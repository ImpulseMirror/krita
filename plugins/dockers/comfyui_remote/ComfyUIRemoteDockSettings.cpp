/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyUIRemoteDock.h"
#include "ComfyUIRemoteDockPrivate.h"
#include "ComfyUIUtils.h"

#include <QApplication>
#include <QClipboard>
#include <QCursor>
#include <QDialog>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QPlainTextEdit>
#include <QScreen>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QStackedWidget>
#include <QLabel>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QPushButton>
#include <QSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QGroupBox>
#include <QMessageBox>
#include <QUrl>
#include <QTabWidget>
#include <QDialogButtonBox>
#include <QFont>
#include <QScrollArea>
#include <QSlider>
#include <QRadioButton>
#include <QButtonGroup>
#include <QFrame>
#include <QLineEdit>
#include <QToolButton>
#include <QMenu>
#include <QAction>
#include <QInputDialog>
#include <QFileDialog>
#include <QFile>
#include <QListWidgetItem>
#include <QAbstractItemView>
#include <QFileInfo>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QSet>
#include <QJsonArray>

#include <KSharedConfig>
#include <KConfigGroup>
#include <klocalizedstring.h>
#include <kis_icon_utils.h>

void ComfyUIRemoteDock::slotConfigureHelp()
{
    if (!m_d->settingsDialog) {
        QDialog *dlg = new QDialog(this);
        dlg->setWindowTitle(i18n("Configure Image Diffusion"));
        dlg->setMinimumSize(960, 480);  // §13.197
        QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
        if (screen) {
            QSize av = screen->availableSize();
            int minW = qMin(av.width(), dlg->fontMetrics().horizontalAdvance(QLatin1Char('M')) * 100);
            dlg->resize(minW, static_cast<int>(av.height() * 0.8));
        } else {
            dlg->resize(960, 480);
        }
        m_d->settingsDialog = dlg;
        // §13.78: When user dismisses Configure (e.g. after connecting), refresh dock so Welcome vs workspace updates
        connect(dlg, &QDialog::finished, this, [this](int) {
            m_d->stylesTabLoraWarningLabel = nullptr;
            m_d->stylesTabLoraListWidget = nullptr;
            updateWelcomeVisibility();
            refreshPromptTagCompleter();
            refreshQueueResolutionRowVisibility();
            updateUpscaleTargetSize();
        });

        QVBoxLayout *mainLayout = new QVBoxLayout(dlg);

        // Top-level horizontal layout: left navigation + right stacked content
        QHBoxLayout *contentLayout = new QHBoxLayout();
        mainLayout->addLayout(contentLayout);

        QListWidget *navList = new QListWidget(dlg);
        navList->setFixedWidth(120);  // §13.175
        navList->setSelectionMode(QAbstractItemView::SingleSelection);
        const QStringList navItems = { i18n("Connection"), i18n("Styles"), i18n("Diffusion"), i18n("Interface"), i18n("Performance"), i18n("Plugin") };
        for (const QString &label : navItems) {
            QListWidgetItem *item = new QListWidgetItem(label);
            item->setSizeHint(QSize(112, 24));  // §13.175
            navList->addItem(item);
        }
        contentLayout->addWidget(navList);

        QStackedWidget *stack = new QStackedWidget(dlg);
        contentLayout->addWidget(stack, 1);

        // §13.33 / §4.4: Connection tab — when ServerMode is undefined show InitialSetupWidget; else show mode selector + panels
        m_d->connectionStack = new QStackedWidget(dlg);

        // Initial setup page (index 0): "Welcome to Image Generation in Krita", three options
        QWidget *initialSetupPage = new QWidget(dlg);
        QVBoxLayout *setupLayout = new QVBoxLayout(initialSetupPage);
        QLabel *setupTitle = new QLabel(i18n("Welcome to Image Generation in Krita"), initialSetupPage);
        QFont setupTitleFont = setupTitle->font();
        setupTitleFont.setPointSize(setupTitleFont.pointSize() + 2);
        setupTitleFont.setBold(true);
        setupTitle->setFont(setupTitleFont);
        setupLayout->addWidget(setupTitle);
        QLabel *setupDesc = new QLabel(i18n("Choose how you want to connect to the image generation backend."), initialSetupPage);
        setupDesc->setWordWrap(true);
        setupLayout->addWidget(setupDesc);
        setupLayout->addSpacing(16);

        auto applyServerMode = [this](const QString &mode) {
            KSharedConfig::openConfig()->group("ComfyUIRemote").writeEntry("ServerMode", mode);
            KSharedConfig::openConfig()->sync();
            if (m_d->connectionStack) {
                m_d->connectionStack->setCurrentIndex(1);
            }
            if (m_d->innerConnectionStack) {
                int idx = (mode == QLatin1String("cloud")) ? 0 : (mode == QLatin1String("managed")) ? 1 : 2;
                m_d->innerConnectionStack->setCurrentIndex(idx);
            }
            if (m_d->connectionModeGroup) {
                QAbstractButton *btn = m_d->connectionModeGroup->button((mode == QLatin1String("cloud")) ? 0 : (mode == QLatin1String("managed")) ? 1 : 2);
                if (btn) btn->setChecked(true);
            }
        };

        QPushButton *btnOnlineService = new QPushButton(i18n("Online Service — Login or Sign up"), initialSetupPage);
        connect(btnOnlineService, &QPushButton::clicked, this, [applyServerMode]() { applyServerMode(QStringLiteral("cloud")); });
        setupLayout->addWidget(btnOnlineService);
        QPushButton *btnManagedServer = new QPushButton(i18n("Local Managed Server — Start Installation"), initialSetupPage);
        connect(btnManagedServer, &QPushButton::clicked, this, [applyServerMode]() { applyServerMode(QStringLiteral("managed")); });
        setupLayout->addWidget(btnManagedServer);
        QPushButton *btnCustomComfy = new QPushButton(i18n("Custom ComfyUI — Connect via URL"), initialSetupPage);
        connect(btnCustomComfy, &QPushButton::clicked, this, [applyServerMode]() { applyServerMode(QStringLiteral("external")); });
        setupLayout->addWidget(btnCustomComfy);
        setupLayout->addStretch();
        m_d->connectionStack->addWidget(initialSetupPage);

        // Mode-selected page (index 1): ServerModeSelect + inner stack (Cloud / Managed / Custom)
        QWidget *modeSelectedPage = new QWidget(dlg);
        QVBoxLayout *modeSelectedLayout = new QVBoxLayout(modeSelectedPage);
        QLabel *connTabHeading = new QLabel(i18n("Server Configuration"), modeSelectedPage);
        QFont connTabHeadingFont = connTabHeading->font();
        connTabHeadingFont.setBold(true);
        connTabHeading->setFont(connTabHeadingFont);
        modeSelectedLayout->addWidget(connTabHeading);
        m_d->connectionModeGroup = new QButtonGroup(modeSelectedPage);
        QHBoxLayout *modeButtonsRow = new QHBoxLayout();
        QRadioButton *radioCloud = new QRadioButton(i18n("Online Service"), modeSelectedPage);
        QRadioButton *radioManaged = new QRadioButton(i18n("Local Managed Server"), modeSelectedPage);
        QRadioButton *radioCustom = new QRadioButton(i18n("Custom Server"), modeSelectedPage);
        m_d->connectionModeGroup->addButton(radioCloud, 0);
        m_d->connectionModeGroup->addButton(radioManaged, 1);
        m_d->connectionModeGroup->addButton(radioCustom, 2);
        modeButtonsRow->addWidget(radioCloud);
        modeButtonsRow->addWidget(radioManaged);
        modeButtonsRow->addWidget(radioCustom);
        modeButtonsRow->addStretch();
        modeSelectedLayout->addLayout(modeButtonsRow);

        m_d->innerConnectionStack = new QStackedWidget(dlg);
        // Cloud panel (index 0) — placeholder
        QWidget *cloudPage = new QWidget(dlg);
        QVBoxLayout *cloudLayout = new QVBoxLayout(cloudPage);
        QLabel *cloudPlaceholder = new QLabel(i18n("Online Service is not available in this build. Use Custom ComfyUI to connect to your own server."), dlg);
        cloudPlaceholder->setWordWrap(true);
        cloudLayout->addWidget(cloudPlaceholder);
        cloudLayout->addStretch();
        m_d->innerConnectionStack->addWidget(cloudPage);
        // Managed panel (index 1) — placeholder
        QWidget *managedPage = new QWidget(dlg);
        QVBoxLayout *managedLayout = new QVBoxLayout(managedPage);
        QLabel *managedPlaceholder = new QLabel(i18n("Local Managed Server is not available in this build. Use Custom ComfyUI to connect to your own server."), dlg);
        managedPlaceholder->setWordWrap(true);
        managedLayout->addWidget(managedPlaceholder);
        managedLayout->addStretch();
        m_d->innerConnectionStack->addWidget(managedPage);

        // Custom Server panel (index 2) — per spec §4.4 (URL → Connect → status → View log files → models → help)
        QWidget *connPage = new QWidget(dlg);
        QVBoxLayout *connLayout = new QVBoxLayout(connPage);

        QLabel *serverUrlDesc = new QLabel(i18n("URL used to connect to a running ComfyUI server. Default is 127.0.0.1:8188 (local)."), connPage);
        serverUrlDesc->setWordWrap(true);
        connLayout->addWidget(serverUrlDesc);
        QFormLayout *connForm = new QFormLayout();
        connForm->addRow(i18n("Server URL:"), m_d->editServerUrl);
        connLayout->addLayout(connForm);

        m_d->btnTest = new QPushButton(i18n("Connect"), connPage);
        m_d->btnTest->setIcon(KisIconUtils::loadIcon("network-connect"));
        connect(m_d->btnTest, &QPushButton::clicked, this, &ComfyUIRemoteDock::slotTestConnection);
        connLayout->addWidget(m_d->btnTest);

        m_d->labelConnectionStatus = new QLabel(i18n("Disconnected"), connPage);
        m_d->labelConnectionStatus->setStyleSheet(QStringLiteral("color: gray;"));
        connLayout->addWidget(m_d->labelConnectionStatus);

        QPushButton *viewLogsButton = new QPushButton(i18n("View log files"), connPage);
        connect(viewLogsButton, &QPushButton::clicked, this, [this]() {
            QDesktopServices::openUrl(QUrl::fromLocalFile(ComfyUIUtils::pluginLogDir()));
        });
        connLayout->addWidget(viewLogsButton);
        // §4.4 / §7.4: Detected base models — list of architectures with supported/missing status (populated when connected)
        QLabel *detectedModelsHeading = new QLabel(i18n("Detected base models:"), connPage);
        connLayout->addWidget(detectedModelsHeading);
        m_d->labelDetectedModels = new QLabel(i18n("Connect to server to see detected architectures (SD 1.5, SD XL, Flux, etc.)."), connPage);
        m_d->labelDetectedModels->setWordWrap(true);
        m_d->labelDetectedModels->setStyleSheet(QStringLiteral("color: gray;"));
        connLayout->addWidget(m_d->labelDetectedModels);
        // §4.4: Help text for required models; Custom ComfyUI Setup = link to docs
        QString connHelpMsg = i18n("See Custom ComfyUI Setup for required models. Check the client.log file for more details.");
        connHelpMsg.replace(QStringLiteral("Custom ComfyUI Setup"),
            QStringLiteral("<a href=\"https://docs.interstice.cloud\">Custom ComfyUI Setup</a>"));
        QLabel *connHelp = new QLabel(connHelpMsg, connPage);
        connHelp->setWordWrap(true);
        connHelp->setTextFormat(Qt::RichText);
        connHelp->setOpenExternalLinks(false);
        connHelp->setTextInteractionFlags(Qt::TextBrowserInteraction);
        connect(connHelp, &QLabel::linkActivated, this, [](const QString &url) {
            QDesktopServices::openUrl(QUrl(url));
        });
        connLayout->addWidget(connHelp);
        connLayout->addStretch();

        m_d->innerConnectionStack->addWidget(connPage);
        modeSelectedLayout->addWidget(m_d->innerConnectionStack, 1);

        connect(m_d->connectionModeGroup, QOverload<QAbstractButton *>::of(&QButtonGroup::buttonClicked), this, [this](QAbstractButton *btn) {
            if (!m_d->innerConnectionStack || !btn) return;
            int id = m_d->connectionModeGroup->id(btn);
            m_d->innerConnectionStack->setCurrentIndex(id);
            QString mode = (id == 0) ? QStringLiteral("cloud") : (id == 1) ? QStringLiteral("managed") : QStringLiteral("external");
            KSharedConfig::openConfig()->group("ComfyUIRemote").writeEntry("ServerMode", mode);
            KSharedConfig::openConfig()->sync();
        });

        m_d->connectionStack->addWidget(modeSelectedPage);
        stack->addWidget(m_d->connectionStack);

        // §13.33: Initial state from config (user may have already chosen a server mode)
        QString initialMode = KSharedConfig::openConfig()->group("ComfyUIRemote").readEntry("ServerMode", QStringLiteral("undefined"));
        if (initialMode != QLatin1String("undefined")) {
            m_d->connectionStack->setCurrentIndex(1);
            int idx = (initialMode == QLatin1String("cloud")) ? 0 : (initialMode == QLatin1String("managed")) ? 1 : 2;
            m_d->innerConnectionStack->setCurrentIndex(idx);
            QAbstractButton *modeBtn = m_d->connectionModeGroup->button(idx);
            if (modeBtn) modeBtn->setChecked(true);
        }

        // Styles tab (index 1) — §4.5 Style Presets (mirrors dock; LoRA list + samplers.json presets partial / stub)
        QWidget *stylesPage = new QWidget(dlg);
        QVBoxLayout *stylesOuter = new QVBoxLayout(stylesPage);
        QScrollArea *stylesScroll = new QScrollArea(stylesPage);
        stylesScroll->setWidgetResizable(true);
        stylesScroll->setFrameShape(QFrame::NoFrame);
        QWidget *stylesInner = new QWidget();
        QVBoxLayout *stylesLayout = new QVBoxLayout(stylesInner);
        QLabel *stylesHeading = new QLabel(i18n("Style Presets"), stylesInner);
        QFont stylesHeadingFont = stylesHeading->font();
        stylesHeadingFont.setBold(true);
        stylesHeading->setFont(stylesHeadingFont);
        stylesLayout->addWidget(stylesHeading);
        QLabel *labelStylePath = new QLabel(stylesInner);
        labelStylePath->setWordWrap(true);
        labelStylePath->setStyleSheet(QStringLiteral("color: palette(mid);"));
        QLabel *stylesBuiltinHint = new QLabel(i18n("Built-in styles cannot be modified. Click to edit a copy."), stylesInner);
        stylesBuiltinHint->setWordWrap(true);
        stylesLayout->addWidget(labelStylePath);
        stylesLayout->addWidget(stylesBuiltinHint);
        QHBoxLayout *presetBtnRow = new QHBoxLayout();
        QComboBox *stylesPresetMirror = new QComboBox(stylesInner);
        // §4.5: dropdown, + (add), ⋮ (menu), trash (delete), refresh, save
        QToolButton *btnStylesAddPreset = new QToolButton(stylesInner);
        btnStylesAddPreset->setIcon(KisIconUtils::loadIcon("list-add"));
        btnStylesAddPreset->setToolTip(i18n("Add a new custom preset from current dock settings"));
        btnStylesAddPreset->setAutoRaise(true);
        QToolButton *btnStylesMenu = new QToolButton(stylesInner);
        btnStylesMenu->setIcon(KisIconUtils::loadIcon("application-menu"));
        btnStylesMenu->setToolTip(i18n("More preset actions"));
        btnStylesMenu->setAutoRaise(true);
        btnStylesMenu->setPopupMode(QToolButton::InstantPopup);
        QMenu *menuStylesPreset = new QMenu(btnStylesMenu);
        QAction *actStylesSaveAsNew = menuStylesPreset->addAction(i18n("Save as new preset…"));
        QAction *actStylesRename = menuStylesPreset->addAction(i18n("Rename preset…"));
        btnStylesMenu->setMenu(menuStylesPreset);
        QToolButton *btnStylesDeletePreset = new QToolButton(stylesInner);
        btnStylesDeletePreset->setIcon(KisIconUtils::loadIcon("edit-delete"));
        btnStylesDeletePreset->setToolTip(i18n("Delete the current custom preset"));
        btnStylesDeletePreset->setAutoRaise(true);
        QPushButton *btnStylesRefresh = new QPushButton(i18n("Refresh"), stylesInner);
        btnStylesRefresh->setToolTip(i18n("Reload the preset list and refresh the checkpoint list from the server"));
        QPushButton *btnStylesSavePreset = new QPushButton(i18n("Save"), stylesInner);
        btnStylesSavePreset->setToolTip(i18n("Save current dock settings into the selected custom preset"));
        connect(btnStylesAddPreset, &QToolButton::clicked, this, &ComfyUIRemoteDock::slotSaveAsPreset);
        connect(actStylesSaveAsNew, &QAction::triggered, this, &ComfyUIRemoteDock::slotSaveAsPreset);
        connect(btnStylesDeletePreset, &QToolButton::clicked, this, &ComfyUIRemoteDock::slotDeletePreset);
        connect(btnStylesSavePreset, &QPushButton::clicked, this, &ComfyUIRemoteDock::slotSaveCurrentPreset);
        presetBtnRow->addWidget(stylesPresetMirror, 1);
        presetBtnRow->addWidget(btnStylesAddPreset);
        presetBtnRow->addWidget(btnStylesMenu);
        presetBtnRow->addWidget(btnStylesDeletePreset);
        presetBtnRow->addWidget(btnStylesRefresh);
        presetBtnRow->addWidget(btnStylesSavePreset);
        stylesLayout->addLayout(presetBtnRow);
        // §4.5: checkbox follows preset toolbar; Name field is the next spec item
        QCheckBox *checkShowBuiltinStyles = new QCheckBox(i18n("Show pre-installed styles"), stylesInner);
        checkShowBuiltinStyles->setChecked(
            ComfyUIUtils::loadSettingsJson().value(QStringLiteral("show_builtin_styles")).toBool(true));
        checkShowBuiltinStyles->setToolTip(i18n("When unchecked, the dock preset list hides built-in entries (custom presets only)."));
        stylesLayout->addWidget(checkShowBuiltinStyles);
        QFormLayout *stylesNameForm = new QFormLayout();
        QLineEdit *editStyleName = new QLineEdit(stylesInner);
        editStyleName->setPlaceholderText(i18n("Custom preset name"));
        stylesNameForm->addRow(i18n("Name:"), editStyleName);
        stylesLayout->addLayout(stylesNameForm);
        QFormLayout *stylesForm = new QFormLayout();
        QHBoxLayout *ckptRow = new QHBoxLayout();
        QComboBox *stylesCkptMirror = new QComboBox(stylesInner);
        stylesCkptMirror->setEditable(true);
        stylesCkptMirror->setPlaceholderText(i18n("The Diffusion model checkpoint file"));
        QToolButton *btnStylesCkptRefresh = new QToolButton(stylesInner);
        btnStylesCkptRefresh->setIcon(KisIconUtils::loadIcon(QStringLiteral("view-refresh")));
        btnStylesCkptRefresh->setToolTip(i18n("Refresh checkpoint list from server"));
        btnStylesCkptRefresh->setAutoRaise(true);
        connect(btnStylesCkptRefresh, &QToolButton::clicked, this, &ComfyUIRemoteDock::slotRefreshCheckpoints);
        ckptRow->addWidget(stylesCkptMirror, 1);
        ckptRow->addWidget(btnStylesCkptRefresh);
        stylesForm->addRow(i18n("Model Checkpoint:"), ckptRow);
        stylesLayout->addLayout(stylesForm);
        stylesCkptMirror->setToolTip(i18n("The Diffusion model checkpoint file."));
        QLabel *stylesCkptWarning = new QLabel(stylesInner);
        stylesCkptWarning->setWordWrap(true);
        stylesCkptWarning->setStyleSheet(QStringLiteral("color: #b8860b;"));
        stylesCkptWarning->hide();
        stylesLayout->addWidget(stylesCkptWarning);

        auto wireDisclosure = [](QToolButton *tb, QWidget *body, bool startOpen) {
            tb->setCheckable(true);
            tb->setChecked(startOpen);
            tb->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
            tb->setArrowType(startOpen ? Qt::DownArrow : Qt::RightArrow);
            tb->setStyleSheet(QStringLiteral("QToolButton { border: none; font-weight: bold; text-align: left; }"));
            body->setVisible(startOpen);
            QObject::connect(tb, &QToolButton::toggled, body, &QWidget::setVisible);
            QObject::connect(tb, &QToolButton::toggled, tb, [tb](bool on) { tb->setArrowType(on ? Qt::DownArrow : Qt::RightArrow); });
        };
        QToolButton *toggleAdvCkpt = new QToolButton(stylesInner);
        toggleAdvCkpt->setText(i18n("Checkpoint configuration (advanced)"));
        QWidget *advCkptBody = new QWidget(stylesInner);
        QVBoxLayout *advCkptLay = new QVBoxLayout(advCkptBody);
        advCkptLay->setContentsMargins(16, 0, 0, 0);
        advCkptLay->addWidget(new QLabel(
            i18n("VAE override, clip skip, and related options are not exposed in this port; use a custom ComfyUI workflow for full control."),
            advCkptBody));
        wireDisclosure(toggleAdvCkpt, advCkptBody, false);
        stylesLayout->addWidget(toggleAdvCkpt);
        stylesLayout->addWidget(advCkptBody);

        QToolButton *toggleLora = new QToolButton(stylesInner);
        toggleLora->setText(i18n("LoRA"));
        QWidget *loraBody = new QWidget(stylesInner);
        QVBoxLayout *loraLay = new QVBoxLayout(loraBody);
        loraLay->setContentsMargins(16, 0, 0, 0);
        loraLay->addWidget(new QLabel(
            i18n("Extensions to the checkpoint which expand its range based on additional training."), loraBody));
        QHBoxLayout *loraBtnRow = new QHBoxLayout();
        QPushButton *btnLoraAdd = new QPushButton(i18n("Add"), loraBody);
        QPushButton *btnLoraRemove = new QPushButton(i18n("Remove"), loraBody);
        QPushButton *btnLoraUpload = new QPushButton(i18n("Upload"), loraBody);
        QComboBox *comboLoraFilter = new QComboBox(loraBody);
        comboLoraFilter->addItem(i18n("All"));
        comboLoraFilter->addItem(i18n("On server"));
        comboLoraFilter->addItem(i18n("Not on server"));
        comboLoraFilter->setToolTip(i18n("Filter using LoRA names from the server object_info list (after Connect or Refresh)."));
        QPushButton *btnLoraRefresh = new QPushButton(i18n("Refresh"), loraBody);
        btnLoraAdd->setToolTip(i18n("Add a local .safetensors LoRA; the basename is used in `<lora:…>` tags appended to positive prompts."));
        btnLoraRemove->setToolTip(i18n("Remove the selected LoRA from the library file."));
        btnLoraUpload->setToolTip(
            i18n("Adds the LoRA to your library. When connected, tries upload via the Krita AI Diffusion ETN API (%1); if that fails or you are offline, copy the file to %2 on the server and use Refresh.",
                 QStringLiteral("PUT api/etn/upload/loras/…"),
                 QStringLiteral("models/loras")));
        btnLoraRefresh->setToolTip(
            i18n("Reload database/loras.json, then refresh checkpoints (updates the server LoRA list from object_info)."));
        loraBtnRow->addWidget(btnLoraAdd);
        loraBtnRow->addWidget(btnLoraRemove);
        loraBtnRow->addWidget(btnLoraUpload);
        loraBtnRow->addWidget(comboLoraFilter);
        loraBtnRow->addWidget(btnLoraRefresh);
        loraBtnRow->addStretch();
        loraLay->addLayout(loraBtnRow);
        QListWidget *listLora = new QListWidget(loraBody);
        listLora->setMaximumHeight(140);
        listLora->setSelectionMode(QAbstractItemView::SingleSelection);
        loraLay->addWidget(listLora);
        QLabel *lblLoraWarning = new QLabel(loraBody);
        lblLoraWarning->setWordWrap(true);
        lblLoraWarning->hide();
        loraLay->addWidget(lblLoraWarning);
        m_d->stylesTabLoraListWidget = listLora;
        m_d->stylesTabLoraWarningLabel = lblLoraWarning;
        QHBoxLayout *loraStrRow = new QHBoxLayout();
        QLabel *lblLoraStr = new QLabel(i18n("Strength %:"), loraBody);
        QSpinBox *spinLoraStrength = new QSpinBox(loraBody);
        spinLoraStrength->setRange(1, 200);
        spinLoraStrength->setValue(100);
        spinLoraStrength->setEnabled(false);
        spinLoraStrength->setToolTip(i18n("Per-LoRA strength as a percentage of 1.0 (e.g. 100 = weight 1.0 in the prompt tag)."));
        loraStrRow->addWidget(lblLoraStr);
        loraStrRow->addWidget(spinLoraStrength);
        loraStrRow->addStretch();
        loraLay->addLayout(loraStrRow);
        loraLay->addWidget(new QLabel(
            i18n("Checked entries are appended to positive prompts as `<lora:filename.safetensors:weight>` for default Generate, Live, Inpaint, and Regions workflows."),
            loraBody));
        wireDisclosure(toggleLora, loraBody, false);
        stylesLayout->addWidget(toggleLora);
        stylesLayout->addWidget(loraBody);

        QLabel *lblSamplerMerge = new QLabel(
            i18n("Configure sampler type, steps and CFG to tweak the quality of generated images. "
                 "Presets match the samplers.json shape (sampler, scheduler, steps, minimum_steps, cfg). "
                 "Optional file %1 merges with built-in presets (reloaded when you open this Styles tab).",
                 ComfyUIUtils::pluginUserDataDir() + QStringLiteral("/presets/samplers.json")),
            stylesInner);
        lblSamplerMerge->setWordWrap(true);
        stylesLayout->addWidget(lblSamplerMerge);
        QLabel *lblControlPresets = new QLabel(
            i18n("Optional %1 defines default strength and range presets for control layers (same structure as the reference plugin). "
                 "Reloaded when you open this tab.",
                 ComfyUIUtils::pluginUserDataDir() + QStringLiteral("/presets/control.json")),
            stylesInner);
        lblControlPresets->setWordWrap(true);
        lblControlPresets->setStyleSheet(QStringLiteral("color: palette(mid);"));
        stylesLayout->addWidget(lblControlPresets);
        QFormLayout *controlPresetForm = new QFormLayout();
        QComboBox *comboControlDefaultPreset = new QComboBox(stylesInner);
        comboControlDefaultPreset->setToolTip(
            i18n("Default strength and timing range when adding a control layer (from control.json, default mode)."));
        controlPresetForm->addRow(i18n("Default control layer preset:"), comboControlDefaultPreset);
        stylesLayout->addLayout(controlPresetForm);
        QComboBox *comboQualitySamplerPreset = new QComboBox(stylesInner);
        QComboBox *comboLiveSamplerPreset = new QComboBox(stylesInner);
        comboQualitySamplerPreset->setToolTip(
            i18n("Applies to Generate, Inpaint, and Regions. Chooses Custom to adjust sampler, steps, and CFG only on the dock."));
        comboLiveSamplerPreset->setToolTip(
            i18n("When a named preset is selected, Live mode uses it for sampler, scheduler, steps, and CFG. Custom follows the dock row."));
        QToolButton *toggleSamplerQuality = new QToolButton(stylesInner);
        toggleSamplerQuality->setText(i18n("Quality Preset (generate and upscale)"));
        QWidget *samplerQualityBody = new QWidget(stylesInner);
        QVBoxLayout *samplerQualityLay = new QVBoxLayout(samplerQualityBody);
        samplerQualityLay->setContentsMargins(16, 0, 0, 0);
        samplerQualityLay->addWidget(new QLabel(
            i18n("Preset used for Generate, Inpaint, Regions, and Upscale (e.g. a named Euler or DPM++ preset)."),
            samplerQualityBody));
        samplerQualityLay->addWidget(comboQualitySamplerPreset);
        wireDisclosure(toggleSamplerQuality, samplerQualityBody, false);
        stylesLayout->addWidget(toggleSamplerQuality);
        stylesLayout->addWidget(samplerQualityBody);
        QToolButton *toggleSamplerLive = new QToolButton(stylesInner);
        toggleSamplerLive->setText(i18n("Performance Preset (live mode)"));
        QWidget *samplerLiveBody = new QWidget(stylesInner);
        QVBoxLayout *samplerLiveLay = new QVBoxLayout(samplerLiveBody);
        samplerLiveLay->setContentsMargins(16, 0, 0, 0);
        samplerLiveLay->addWidget(new QLabel(
            i18n("Preset used for Live mode, or Custom to follow the dock sampler row."),
            samplerLiveBody));
        samplerLiveLay->addWidget(comboLiveSamplerPreset);
        wireDisclosure(toggleSamplerLive, samplerLiveBody, false);
        stylesLayout->addWidget(toggleSamplerLive);
        stylesLayout->addWidget(samplerLiveBody);

        QLabel *stylePromptDesc = new QLabel(
            i18n("Text which is appended to all prompts. The {prompt} placeholder can be used to wrap prompts."), stylesInner);
        stylePromptDesc->setWordWrap(true);
        stylesLayout->addWidget(stylePromptDesc);
        QPlainTextEdit *editStylesPositive = new QPlainTextEdit(stylesInner);
        editStylesPositive->setPlaceholderText(i18n("Style prompt (synced with Generate view)"));
        editStylesPositive->setMaximumHeight(100);
        stylesLayout->addWidget(editStylesPositive);
        QLabel *negDesc = new QLabel(i18n("Textual description of things to avoid in generated images."), stylesInner);
        negDesc->setWordWrap(true);
        stylesLayout->addWidget(negDesc);
        QPlainTextEdit *editStylesNegative = new QPlainTextEdit(stylesInner);
        editStylesNegative->setPlaceholderText(i18n("Negative prompt (synced with Generate view)"));
        editStylesNegative->setMaximumHeight(80);
        stylesLayout->addWidget(editStylesNegative);
        QFormLayout *linkedStyleForm = new QFormLayout();
        QComboBox *comboLinkedEditStyle = new QComboBox(stylesInner);
        comboLinkedEditStyle->setToolTip(i18n("Select an alternative style to use for instruction-based editing."));
        linkedStyleForm->addRow(i18n("Linked Edit Style:"), comboLinkedEditStyle);
        stylesLayout->addLayout(linkedStyleForm);
        // §4.2: Six nav items only — Fast/Quality and custom workflow live under Styles (not a seventh nav entry).
        QFormLayout *qualityFastForm = new QFormLayout();
        qualityFastForm->addRow(i18n("Quality:"), m_d->comboQuality);
        stylesLayout->addLayout(qualityFastForm);
        {
            QLabel *dockGenHint = new QLabel(
                i18n("Image size (preset, width, height), sampling steps, CFG scale, sampler, fixed seed, and random seed are set on the AI Image Generation dock (Generate workspace)."),
                stylesInner);
            dockGenHint->setWordWrap(true);
            stylesLayout->addWidget(dockGenHint);
        }
        QGroupBox *workflowGroup = new QGroupBox(i18n("Custom ComfyUI workflow"), stylesInner);
        QVBoxLayout *workflowLayout = new QVBoxLayout(workflowGroup);
        workflowLayout->addWidget(new QLabel(i18n("Custom workflow (optional, API JSON):"), workflowGroup));
        QLabel *wfFolderInfo = new QLabel(
            i18n("Local workflow library: %1 (place exported API JSON files here).", ComfyUIUtils::workflowsStorageDir()),
            workflowGroup);
        wfFolderInfo->setWordWrap(true);
        workflowLayout->addWidget(wfFolderInfo);
        QHBoxLayout *wfLibRow = new QHBoxLayout();
        QComboBox *comboLocalWorkflows = new QComboBox(workflowGroup);
        QPushButton *btnWorkflowRefresh = new QPushButton(i18n("Refresh list"), workflowGroup);
        QPushButton *btnWorkflowFolder = new QPushButton(i18n("Open folder"), workflowGroup);
        wfLibRow->addWidget(new QLabel(i18n("Load from library:"), workflowGroup));
        wfLibRow->addWidget(comboLocalWorkflows, 1);
        wfLibRow->addWidget(btnWorkflowRefresh);
        wfLibRow->addWidget(btnWorkflowFolder);
        workflowLayout->addLayout(wfLibRow);
        auto refillLocalWorkflowCombo = [comboLocalWorkflows]() {
            comboLocalWorkflows->blockSignals(true);
            comboLocalWorkflows->clear();
            comboLocalWorkflows->addItem(i18n("— select —"), QString());
            for (const QString &n : ComfyUIUtils::listLocalWorkflowJsonFilenames())
                comboLocalWorkflows->addItem(n, n);
            comboLocalWorkflows->setCurrentIndex(0);
            comboLocalWorkflows->blockSignals(false);
        };
        refillLocalWorkflowCombo();
        connect(comboLocalWorkflows, QOverload<int>::of(&QComboBox::activated), this,
                [this, comboLocalWorkflows](int idx) {
                    if (idx <= 0 || !m_d->editCustomWorkflow)
                        return;
                    const QString fn = comboLocalWorkflows->itemData(idx).toString();
                    if (fn.isEmpty())
                        return;
                    QFile f(ComfyUIUtils::workflowsStorageDir() + QLatin1Char('/') + fn);
                    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
                        return;
                    m_d->editCustomWorkflow->setPlainText(
                        QString::fromUtf8(ComfyUIUtils::stripJsonLineComments(f.readAll())));
                });
        connect(btnWorkflowRefresh, &QPushButton::clicked, dlg, [refillLocalWorkflowCombo]() { refillLocalWorkflowCombo(); });
        connect(btnWorkflowFolder, &QPushButton::clicked, dlg, []() {
            QDesktopServices::openUrl(QUrl::fromLocalFile(ComfyUIUtils::workflowsStorageDir()));
        });
        workflowLayout->addWidget(m_d->checkUseReferenceImage);
        workflowLayout->addWidget(m_d->editCustomWorkflow);
        m_d->customWorkflowParamsGroup = new QGroupBox(i18n("Workflow parameters (ETN)"), workflowGroup);
        m_d->customWorkflowParamsForm = new QFormLayout(m_d->customWorkflowParamsGroup);
        m_d->customWorkflowParamsGroup->setLayout(m_d->customWorkflowParamsForm);
        workflowLayout->addWidget(m_d->customWorkflowParamsGroup);
        m_d->customWorkflowParamsGroup->setVisible(false);
        workflowLayout->addWidget(m_d->btnLoadWorkflow);
        workflowLayout->addStretch();
        stylesLayout->addWidget(workflowGroup);
        stylesLayout->addStretch();
        stylesScroll->setWidget(stylesInner);
        stylesOuter->addWidget(stylesScroll);
        stack->addWidget(stylesPage);

        auto persistLoraList = [listLora]() {
            QJsonArray arr;
            for (int i = 0; i < listLora->count(); ++i) {
                QListWidgetItem *it = listLora->item(i);
                if (!it)
                    continue;
                QJsonObject o;
                o.insert(QStringLiteral("filename"), it->data(Qt::UserRole).toString());
                o.insert(QStringLiteral("strength_percent"), it->data(Qt::UserRole + 1).toInt());
                o.insert(QStringLiteral("enabled"), it->checkState() == Qt::Checked);
                arr.append(o);
            }
            ComfyUIUtils::saveLorasJsonArray(arr);
        };
        auto reloadLoraListFromDisk = [listLora, spinLoraStrength, this]() {
            listLora->blockSignals(true);
            listLora->clear();
            const QJsonArray arr = ComfyUIUtils::loadLorasJsonArray();
            for (const QJsonValue &v : arr) {
                const QJsonObject o = v.toObject();
                QString fn = o.value(QStringLiteral("filename")).toString().trimmed();
                if (fn.isEmpty())
                    fn = o.value(QStringLiteral("name")).toString().trimmed();
                if (fn.isEmpty())
                    continue;
                const int pct = o.value(QStringLiteral("strength_percent")).toInt(100);
                const bool en = o.value(QStringLiteral("enabled")).toBool(true);
                auto *it = new QListWidgetItem(QStringLiteral("%1 — %2%").arg(fn).arg(pct), listLora);
                it->setFlags(it->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
                it->setCheckState(en ? Qt::Checked : Qt::Unchecked);
                it->setData(Qt::UserRole, fn);
                it->setData(Qt::UserRole + 1, qBound(1, pct, 200));
                listLora->addItem(it);
            }
            listLora->blockSignals(false);
            if (listLora->currentRow() < 0 && listLora->count() > 0)
                listLora->setCurrentRow(0);
            QListWidgetItem *cur = listLora->currentItem();
            spinLoraStrength->setEnabled(cur != nullptr);
            if (cur) {
                spinLoraStrength->blockSignals(true);
                spinLoraStrength->setValue(cur->data(Qt::UserRole + 1).toInt());
                spinLoraStrength->blockSignals(false);
            }
            applyStylesTabLoraListFilter();
            refreshStylesTabLoraWarning();
        };
        comboLoraFilter->setCurrentIndex(m_d->stylesTabLoraFilterMode);
        reloadLoraListFromDisk();
        connect(btnLoraAdd, &QPushButton::clicked, dlg, [listLora, loraBody, persistLoraList]() {
            const QString path = QFileDialog::getOpenFileName(
                loraBody,
                i18n("Select LoRA file"),
                QString(),
                i18n("LoRA files (*.safetensors *.pt);;All files (*)"));
            if (path.isEmpty())
                return;
            const QString base = QFileInfo(path).fileName();
            if (base.isEmpty())
                return;
            for (int i = 0; i < listLora->count(); ++i) {
                if (listLora->item(i)->data(Qt::UserRole).toString() == base)
                    return;
            }
            auto *it = new QListWidgetItem(QStringLiteral("%1 — 100%").arg(base), listLora);
            it->setFlags(it->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
            it->setCheckState(Qt::Checked);
            it->setData(Qt::UserRole, base);
            it->setData(Qt::UserRole + 1, 100);
            listLora->addItem(it);
            listLora->setCurrentItem(it);
            persistLoraList();
            applyStylesTabLoraListFilter();
            refreshStylesTabLoraWarning();
        });
        connect(btnLoraRemove, &QPushButton::clicked, dlg, [listLora, spinLoraStrength, persistLoraList, this]() {
            const int row = listLora->currentRow();
            if (row < 0)
                return;
            delete listLora->takeItem(row);
            persistLoraList();
            spinLoraStrength->setEnabled(listLora->currentItem() != nullptr);
            applyStylesTabLoraListFilter();
            refreshStylesTabLoraWarning();
        });
        connect(btnLoraRefresh, &QPushButton::clicked, this, [this, reloadLoraListFromDisk]() {
            reloadLoraListFromDisk();
            slotRefreshCheckpoints();
        });
        connect(btnLoraUpload, &QPushButton::clicked, dlg, [listLora, loraBody, persistLoraList, dlg, this]() {
            const QString path = QFileDialog::getOpenFileName(
                loraBody,
                i18n("Select LoRA file"),
                QString(),
                i18n("LoRA files (*.safetensors);;LoRA files (*.safetensors *.pt);;All files (*)"));
            if (path.isEmpty())
                return;
            const QString base = QFileInfo(path).fileName();
            if (base.isEmpty())
                return;
            for (int i = 0; i < listLora->count(); ++i) {
                if (listLora->item(i)->data(Qt::UserRole).toString() == base)
                    return;
            }
            auto *it = new QListWidgetItem(QStringLiteral("%1 — 100%").arg(base), listLora);
            it->setFlags(it->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
            it->setCheckState(Qt::Checked);
            it->setData(Qt::UserRole, base);
            it->setData(Qt::UserRole + 1, 100);
            listLora->addItem(it);
            listLora->setCurrentItem(it);
            persistLoraList();
            applyStylesTabLoraListFilter();
            refreshStylesTabLoraWarning();

            const QString url = m_d->editServerUrl ? m_d->editServerUrl->text().trimmed() : QString();
            if (m_d->isConnected && m_d->nam && !url.isEmpty()) {
                QPointer<QDialog> dlgLife(dlg);
                QNetworkReply *reply = ComfyUIUtils::tryUploadLoraFileViaEtnApi(m_d->nam, url, path, dlg);
                if (!reply) {
                    QMessageBox::warning(
                        loraBody,
                        i18n("Upload LoRA"),
                        i18n("Could not read the selected file. The library entry was still saved."));
                    return;
                }
                connect(reply, &QNetworkReply::finished, dlg, [reply, dlgLife, this]() {
                    reply->deleteLater();
                    if (!dlgLife)
                        return;
                    const QVariant codeVar = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
                    const int code = codeVar.isValid() ? codeVar.toInt() : 0;
                    const bool ok = (reply->error() == QNetworkReply::NoError && (code == 200 || code == 201 || code == 204));
                    if (ok) {
                        slotRefreshCheckpoints();
                        QMessageBox::information(
                            dlgLife.data(),
                            i18n("Upload LoRA"),
                            i18n("The server accepted the LoRA upload (Krita AI Diffusion ETN API)."));
                    } else {
                        const QString codeStr = code > 0 ? QString::number(code) : QStringLiteral("—");
                        QMessageBox::information(
                            dlgLife.data(),
                            i18n("Upload LoRA"),
                            i18n("The filename was saved in your library, but the server did not accept the upload (HTTP %1). "
                                 "Use a ComfyUI setup with the Krita AI Diffusion nodes, or copy the file to %2 on the server and click Refresh.",
                                 codeStr,
                                 QStringLiteral("models/loras")));
                    }
                });
                return;
            }
            QMessageBox::information(
                loraBody,
                i18n("Upload LoRA"),
                i18n("Connect to the server to try an automatic upload (ETN API), or copy this file into the server’s %1 folder, then click Refresh.",
                     QStringLiteral("models/loras")));
        });
        connect(comboLoraFilter, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
            m_d->stylesTabLoraFilterMode = idx;
            applyStylesTabLoraListFilter();
        });
        connect(listLora, &QListWidget::itemChanged, dlg, [persistLoraList](QListWidgetItem *) { persistLoraList(); });
        connect(listLora, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *, QListWidgetItem *) {
            refreshStylesTabLoraWarning();
        });
        connect(listLora, &QListWidget::currentRowChanged, dlg, [listLora, spinLoraStrength](int row) {
            if (row < 0 || !listLora->item(row)) {
                spinLoraStrength->setEnabled(false);
                return;
            }
            spinLoraStrength->setEnabled(true);
            spinLoraStrength->blockSignals(true);
            spinLoraStrength->setValue(listLora->item(row)->data(Qt::UserRole + 1).toInt());
            spinLoraStrength->blockSignals(false);
        });
        connect(spinLoraStrength, QOverload<int>::of(&QSpinBox::valueChanged), dlg,
                [listLora, spinLoraStrength, persistLoraList](int v) {
                    QListWidgetItem *it = listLora->currentItem();
                    if (!it)
                        return;
                    it->setData(Qt::UserRole + 1, v);
                    const QString fn = it->data(Qt::UserRole).toString();
                    it->setText(QStringLiteral("%1 — %2%").arg(fn).arg(v));
                    persistLoraList();
                });

        QString stylesTabPresetNameBaseline;
        bool syncingStylesTab = false;
        auto repopulateLinkedEditStyleCombo = [comboLinkedEditStyle]() {
            const QJsonObject settings = ComfyUIUtils::loadSettingsJson();
            const QString saved = settings.value(QStringLiteral("linked_edit_style")).toString().trimmed();
            comboLinkedEditStyle->blockSignals(true);
            comboLinkedEditStyle->clear();
            comboLinkedEditStyle->addItem(i18n("None"), QString());
            KConfigGroup cfg(KSharedConfig::openConfig(), QStringLiteral("ComfyUIRemote"));
            for (const QString &n : cfg.readEntry(QStringLiteral("PresetNames"), QStringList())) {
                if (!n.isEmpty())
                    comboLinkedEditStyle->addItem(n, n);
            }
            int si = 0;
            if (!saved.isEmpty()) {
                const int fi = comboLinkedEditStyle->findData(saved);
                if (fi >= 0)
                    si = fi;
            }
            comboLinkedEditStyle->setCurrentIndex(si);
            comboLinkedEditStyle->blockSignals(false);
        };
        auto syncStyleNameField = [editStyleName, &stylesTabPresetNameBaseline, this]() {
            if (!m_d->comboPreset)
                return;
            editStyleName->blockSignals(true);
            const int idx = m_d->comboPreset->currentIndex();
            const int firstCustom = firstCustomPresetIndex();
            if (idx <= 0) {
                editStyleName->clear();
                editStyleName->setReadOnly(true);
            } else if (idx < firstCustom) {
                editStyleName->setText(m_d->comboPreset->currentText());
                editStyleName->setReadOnly(true);
            } else {
                editStyleName->setText(m_d->comboPreset->currentText());
                editStyleName->setReadOnly(false);
                stylesTabPresetNameBaseline = m_d->comboPreset->currentText();
            }
            editStyleName->blockSignals(false);
        };
        auto updateStylePathLabel = [labelStylePath, this]() {
            if (!m_d->comboPreset) return;
            const int idx = m_d->comboPreset->currentIndex();
            const int firstCustom = firstCustomPresetIndex();
            QString pathPart;
            if (idx <= 0)
                pathPart = i18n("no preset");
            else if (idx < firstCustom) {
                const char *ids[] = {"", "portrait", "landscape", "anime", "realistic"};
                pathPart = QStringLiteral("built-in/%1.json").arg(QLatin1String(ids[idx]));
            } else {
                pathPart = i18n("custom (Krita configuration)");
            }
            labelStylePath->setText(i18n("Current preset: %1 (%2)", m_d->comboPreset->currentText(), pathPart));
        };
        auto updateStylesCkptWarning = [stylesCkptWarning, this]() {
            if (!m_d->comboCheckpoint) {
                stylesCkptWarning->hide();
                return;
            }
            const QString ck = m_d->comboCheckpoint->currentText().trimmed();
            if (ck.isEmpty()) {
                stylesCkptWarning->hide();
                return;
            }
            bool known = false;
            for (int i = 0; i < m_d->comboCheckpoint->count(); ++i) {
                if (m_d->comboCheckpoint->itemText(i) == ck) {
                    known = true;
                    break;
                }
            }
            if (!known) {
                stylesCkptWarning->setText(i18n("The checkpoint used by this style is not installed."));
                stylesCkptWarning->show();
            } else {
                stylesCkptWarning->hide();
            }
        };
        auto syncSamplerPresetCombos = [comboQualitySamplerPreset, comboLiveSamplerPreset, comboControlDefaultPreset]() {
            ComfyUIUtils::reloadSamplerPresetsCache();
            ComfyUIUtils::reloadControlPresetsCache();
            const QJsonObject root = ComfyUIUtils::builtinSamplerPresetsRoot();
            QStringList keys = root.keys();
            keys.sort();
            const QJsonObject s = ComfyUIUtils::loadSettingsJson();
            const QString savedQ = s.value(QStringLiteral("quality_sampler_preset")).toString();
            const QString savedL = s.value(QStringLiteral("live_sampler_preset")).toString();
            auto refill = [&](QComboBox *cb, const QString &savedKey) {
                cb->blockSignals(true);
                cb->clear();
                cb->addItem(i18n("Custom (use dock controls)"), QString());
                for (const QString &k : keys) {
                    const QJsonObject o = root.value(k).toObject();
                    if (o.isEmpty())
                        continue;
                    if (o.value(QStringLiteral("hidden")).toBool())
                        continue;
                    cb->addItem(k, k);
                }
                const int idx = savedKey.isEmpty() ? 0 : cb->findData(savedKey);
                cb->setCurrentIndex(idx >= 0 ? idx : 0);
                cb->blockSignals(false);
            };
            refill(comboQualitySamplerPreset, savedQ);
            refill(comboLiveSamplerPreset, savedL);

            comboControlDefaultPreset->blockSignals(true);
            comboControlDefaultPreset->clear();
            const QJsonObject controlRoot = ComfyUIUtils::builtinControlPresetsRoot();
            const QList<ComfyUIUtils::ControlLayerPreset> cps =
                ComfyUIUtils::controlPresetsForMode(controlRoot, QStringLiteral("default"), QString());
            const int maxSlots = qMin(4, cps.size());
            if (maxSlots <= 0) {
                comboControlDefaultPreset->addItem(i18n("No presets in control.json"), -1);
                comboControlDefaultPreset->setEnabled(false);
            } else {
                comboControlDefaultPreset->setEnabled(true);
                for (int i = 0; i < maxSlots; ++i) {
                    const ComfyUIUtils::ControlLayerPreset &p = cps.at(i);
                    comboControlDefaultPreset->addItem(
                        i18n("Preset %1: strength %2, range %3–%4",
                             i + 1,
                             QString::number(p.strength, 'f', 2),
                             QString::number(p.start, 'f', 2),
                             QString::number(p.end, 'f', 2)),
                        i);
                }
                const int want = qBound(0,
                                        s.value(QStringLiteral("control_layer_default_preset_index")).toInt(0),
                                        maxSlots - 1);
                comboControlDefaultPreset->setCurrentIndex(want);
            }
            comboControlDefaultPreset->blockSignals(false);
        };
        auto syncStylesFromDock = [this, stylesPresetMirror, stylesCkptMirror, editStylesPositive, editStylesNegative,
                                    updateStylePathLabel, updateStylesCkptWarning, repopulateLinkedEditStyleCombo,
                                    syncStyleNameField, syncSamplerPresetCombos, reloadLoraListFromDisk,
                                    &syncingStylesTab]() {
            if (!m_d->comboPreset) return;
            syncingStylesTab = true;
            stylesPresetMirror->blockSignals(true);
            stylesPresetMirror->clear();
            for (int i = 0; i < m_d->comboPreset->count(); ++i)
                stylesPresetMirror->addItem(m_d->comboPreset->itemText(i), i);
            int cur = m_d->comboPreset->currentIndex();
            int mirrorIdx = stylesPresetMirror->findData(cur);
            stylesPresetMirror->setCurrentIndex(mirrorIdx >= 0 ? mirrorIdx : 0);
            stylesPresetMirror->blockSignals(false);
            if (m_d->comboCheckpoint) {
                stylesCkptMirror->blockSignals(true);
                stylesCkptMirror->clear();
                for (int i = 0; i < m_d->comboCheckpoint->count(); ++i)
                    stylesCkptMirror->addItem(m_d->comboCheckpoint->itemText(i));
                stylesCkptMirror->setCurrentIndex(m_d->comboCheckpoint->currentIndex());
                if (m_d->comboCheckpoint->currentText().size()
                    && stylesCkptMirror->findText(m_d->comboCheckpoint->currentText()) < 0)
                    stylesCkptMirror->setEditText(m_d->comboCheckpoint->currentText());
                stylesCkptMirror->blockSignals(false);
            }
            editStylesPositive->setPlainText(m_d->editPrompt ? m_d->editPrompt->toPlainText() : QString());
            editStylesNegative->setPlainText(m_d->editNegative ? m_d->editNegative->toPlainText() : QString());
            syncingStylesTab = false;
            updateStylePathLabel();
            updateStylesCkptWarning();
            repopulateLinkedEditStyleCombo();
            syncStyleNameField();
            syncSamplerPresetCombos();
            reloadLoraListFromDisk();
        };
        connect(actStylesRename, &QAction::triggered, this, [this, syncStylesFromDock]() {
            if (!m_d->comboPreset)
                return;
            const int idx = m_d->comboPreset->currentIndex();
            if (idx < firstCustomPresetIndex()) {
                QMessageBox::information(this, i18n("Rename preset"), i18n("Select a custom preset to rename."));
                return;
            }
            const QString oldName = m_d->comboPreset->currentText();
            bool ok = false;
            const QString newName = QInputDialog::getText(this, i18n("Rename preset"), i18n("New name:"), QLineEdit::Normal,
                                                          oldName, &ok)
                                        .trimmed();
            if (!ok || newName.isEmpty() || newName == oldName)
                return;
            if (!renameCustomPreset(oldName, newName)) {
                QMessageBox::warning(this, i18n("Rename preset"), i18n("Could not rename (name may already exist)."));
                return;
            }
            syncStylesFromDock();
        });
        connect(btnStylesRefresh, &QPushButton::clicked, this, [this, syncStylesFromDock]() {
            rebuildPresetComboItems();
            slotRefreshCheckpoints();
            syncStylesFromDock();
        });
        connect(menuStylesPreset, &QMenu::aboutToShow, this, [actStylesRename, this]() {
            const bool custom = m_d->comboPreset && m_d->comboPreset->currentIndex() >= firstCustomPresetIndex();
            actStylesRename->setEnabled(custom);
        });
        connect(comboQualitySamplerPreset, QOverload<int>::of(&QComboBox::currentIndexChanged), dlg,
                [this, comboQualitySamplerPreset]() {
                    const QString k = comboQualitySamplerPreset->currentData().toString();
                    QJsonObject s = ComfyUIUtils::loadSettingsJson();
                    s.insert(QStringLiteral("quality_sampler_preset"), k);
                    ComfyUIUtils::saveSettingsJson(s);
                    applyQualitySamplerPresetKey(k);
                });
        connect(comboLiveSamplerPreset, QOverload<int>::of(&QComboBox::currentIndexChanged), dlg,
                [comboLiveSamplerPreset]() {
                    QJsonObject s = ComfyUIUtils::loadSettingsJson();
                    s.insert(QStringLiteral("live_sampler_preset"), comboLiveSamplerPreset->currentData().toString());
                    ComfyUIUtils::saveSettingsJson(s);
                });
        connect(comboControlDefaultPreset, QOverload<int>::of(&QComboBox::currentIndexChanged), dlg,
                [comboControlDefaultPreset]() {
                    const int idx = comboControlDefaultPreset->currentData().toInt();
                    if (idx < 0)
                        return;
                    QJsonObject st = ComfyUIUtils::loadSettingsJson();
                    st.insert(QStringLiteral("control_layer_default_preset_index"), idx);
                    ComfyUIUtils::saveSettingsJson(st);
                });
        connect(stylesPresetMirror, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this, stylesPresetMirror, updateStylePathLabel, updateStylesCkptWarning, syncStyleNameField](int) {
                int dataIdx = stylesPresetMirror->currentData().toInt();
                if (m_d->comboPreset && dataIdx >= 0 && dataIdx < m_d->comboPreset->count())
                    m_d->comboPreset->setCurrentIndex(dataIdx);
                updateStylePathLabel();
                updateStylesCkptWarning();
                syncStyleNameField();
            });
        connect(stylesCkptMirror, &QComboBox::currentTextChanged, this, [this, updateStylesCkptWarning](const QString &t) {
            if (m_d->comboCheckpoint) {
                int fi = m_d->comboCheckpoint->findText(t);
                if (fi >= 0) m_d->comboCheckpoint->setCurrentIndex(fi);
                else m_d->comboCheckpoint->setCurrentText(t);
            }
            updateStylesCkptWarning();
        });
        connect(editStylesPositive, &QPlainTextEdit::textChanged, this, [this, editStylesPositive, &syncingStylesTab]() {
            if (!syncingStylesTab && m_d->editPrompt)
                m_d->editPrompt->setPlainText(editStylesPositive->toPlainText());
        });
        connect(editStylesNegative, &QPlainTextEdit::textChanged, this, [this, editStylesNegative, &syncingStylesTab]() {
            if (!syncingStylesTab && m_d->editNegative)
                m_d->editNegative->setPlainText(editStylesNegative->toPlainText());
        });
        connect(checkShowBuiltinStyles, &QCheckBox::toggled, dlg, [this, syncStylesFromDock](bool on) {
            QJsonObject s = ComfyUIUtils::loadSettingsJson();
            s.insert(QStringLiteral("show_builtin_styles"), on);
            ComfyUIUtils::saveSettingsJson(s);
            rebuildPresetComboItems();
            syncStylesFromDock();
        });
        connect(comboLinkedEditStyle, QOverload<int>::of(&QComboBox::currentIndexChanged), dlg, [comboLinkedEditStyle]() {
            QJsonObject s = ComfyUIUtils::loadSettingsJson();
            s.insert(QStringLiteral("linked_edit_style"), comboLinkedEditStyle->currentData().toString());
            ComfyUIUtils::saveSettingsJson(s);
        });
        connect(editStyleName, &QLineEdit::editingFinished, this,
                [this, editStyleName, &stylesTabPresetNameBaseline, syncStylesFromDock]() {
                    if (!m_d->comboPreset)
                        return;
                    const int idx = m_d->comboPreset->currentIndex();
                    if (idx < firstCustomPresetIndex())
                        return;
                    const QString oldName = stylesTabPresetNameBaseline.trimmed();
                    const QString newName = editStyleName->text().trimmed();
                    if (newName.isEmpty()) {
                        editStyleName->blockSignals(true);
                        editStyleName->setText(oldName);
                        editStyleName->blockSignals(false);
                        return;
                    }
                    if (oldName == newName)
                        return;
                    if (!renameCustomPreset(oldName, newName)) {
                        editStyleName->blockSignals(true);
                        editStyleName->setText(oldName);
                        editStyleName->blockSignals(false);
                        return;
                    }
                    syncStylesFromDock();
                });

        // Diffusion tab (index 2) – §4.6: Selection Feather, Blend, Padding, Color Match, NSFW Filter; §13.43 advanced (min transition, grow offset, invert, square)
        QWidget *diffusionPage = new QWidget(dlg);
        QVBoxLayout *diffusionLayout = new QVBoxLayout(diffusionPage);
        QLabel *diffHeading = new QLabel(i18n("Diffusion Settings"), diffusionPage);
        QFont diffFont = diffHeading->font();
        diffFont.setBold(true);
        diffHeading->setFont(diffFont);
        diffusionLayout->addWidget(diffHeading);
        QJsonObject diffSettings = ComfyUIUtils::loadSettingsJson();
        // §4.6 / §3.5: spec defaults 10, 25, 6; feather UI range 0–25
        int selFeather = qBound(0, diffSettings.value(QStringLiteral("selection_feather")).toInt(10), 25);
        int selBlend = qBound(0, diffSettings.value(QStringLiteral("selection_blend")).toInt(25), 100);
        int selPadding = qBound(0, diffSettings.value(QStringLiteral("selection_padding")).toInt(6), 25);
        bool colorMatch = diffSettings.value(QStringLiteral("color_match")).toBool(true);
        double nsfwVal = qBound(0.0, diffSettings.value(QStringLiteral("nsfw_filter")).toDouble(0.0), 1.0);
        double selMinTransition = qBound(0.0, diffSettings.value(QStringLiteral("selection_min_transition")).toDouble(0.0), 100.0);
        int selGrowOffset = qBound(0, diffSettings.value(QStringLiteral("selection_grow_offset")).toInt(0), 499);
        const bool selInvert = ComfyUIUtils::getSelectionModifiersInvert();
        const bool selSquare = ComfyUIUtils::getSelectionModifiersSquare();
        QFormLayout *diffForm = new QFormLayout();
        // §4.6: Selection Feather — slider 0–25, suffix " %"
        QSlider *sliderSelectionFeather = new QSlider(Qt::Horizontal, diffusionPage);
        sliderSelectionFeather->setRange(0, 25);
        sliderSelectionFeather->setValue(selFeather);
        sliderSelectionFeather->setToolTip(i18n("The border is expanded and blurred by a fraction of selection size."));
        QLabel *labelFeatherVal = new QLabel(QStringLiteral("%1 %").arg(selFeather), diffusionPage);
        QHBoxLayout *featherRow = new QHBoxLayout();
        featherRow->addWidget(sliderSelectionFeather, 1);
        featherRow->addWidget(labelFeatherVal);
        diffForm->addRow(i18n("Selection Feather:"), featherRow);
        // §4.6: Selection Blend — slider 0–100, suffix " px"
        QSlider *sliderSelectionBlend = new QSlider(Qt::Horizontal, diffusionPage);
        sliderSelectionBlend->setRange(0, 100);
        sliderSelectionBlend->setValue(selBlend);
        sliderSelectionBlend->setToolTip(i18n("Transition area for alpha blending the result image."));
        QLabel *labelBlendVal = new QLabel(QString::number(selBlend) + i18n(" px"), diffusionPage);
        QHBoxLayout *blendRow = new QHBoxLayout();
        blendRow->addWidget(sliderSelectionBlend, 1);
        blendRow->addWidget(labelBlendVal);
        diffForm->addRow(i18n("Selection Blend:"), blendRow);
        // §4.6: Selection Padding — slider 0–25, suffix " %"
        QSlider *sliderSelectionPadding = new QSlider(Qt::Horizontal, diffusionPage);
        sliderSelectionPadding->setRange(0, 25);
        sliderSelectionPadding->setValue(selPadding);
        sliderSelectionPadding->setToolTip(i18n("Minimum additional padding around the selection area."));
        QLabel *labelPaddingVal = new QLabel(QStringLiteral("%1 %").arg(selPadding), diffusionPage);
        QHBoxLayout *paddingRow = new QHBoxLayout();
        paddingRow->addWidget(sliderSelectionPadding, 1);
        paddingRow->addWidget(labelPaddingVal);
        diffForm->addRow(i18n("Selection Padding:"), paddingRow);
        // §4.6: Color Match toggle
        QCheckBox *checkColorMatch = new QCheckBox(i18n("Color Match"), diffusionPage);
        checkColorMatch->setChecked(colorMatch);
        checkColorMatch->setToolTip(i18n("Match peripheral colors and brightness with existing content. Requires a selection."));
        diffForm->addRow(QString(), checkColorMatch);
        // §4.6: NSFW Filter dropdown Disabled / Basic / Strict
        QComboBox *comboNsfwFilter = new QComboBox(diffusionPage);
        comboNsfwFilter->addItem(i18n("Disabled"), 0.0);
        comboNsfwFilter->addItem(i18n("Basic"), 0.65);
        comboNsfwFilter->addItem(i18n("Strict"), 0.8);
        int nsfwIdx = (nsfwVal <= 0.0) ? 0 : (nsfwVal < 0.7) ? 1 : 2;
        comboNsfwFilter->setCurrentIndex(nsfwIdx);
        comboNsfwFilter->setToolTip(i18n("Attempt to filter out images with explicit content."));
        diffForm->addRow(i18n("NSFW Filter:"), comboNsfwFilter);
        diffusionLayout->addLayout(diffForm);
        // Advanced: §13.43 selection_min_transition, selection_grow_offset; §13.102 invert, square (not in Python Diffusion tab but required in schema)
        QGroupBox *advancedGroup = new QGroupBox(i18n("Advanced (mask preprocess)"), diffusionPage);
        QFormLayout *advForm = new QFormLayout(advancedGroup);
        QDoubleSpinBox *spinSelectionMinTransition = new QDoubleSpinBox(diffusionPage);
        spinSelectionMinTransition->setRange(0, 100);
        spinSelectionMinTransition->setDecimals(1);
        spinSelectionMinTransition->setValue(selMinTransition);
        spinSelectionMinTransition->setToolTip(i18n("Minimum transition size in pixels (scaled by strength)."));
        advForm->addRow(i18n("Selection min transition:"), spinSelectionMinTransition);
        QSpinBox *spinSelectionGrowOffset = new QSpinBox(diffusionPage);
        spinSelectionGrowOffset->setRange(0, 499);
        spinSelectionGrowOffset->setValue(selGrowOffset);
        spinSelectionGrowOffset->setToolTip(i18n("Extra grow offset in pixels for mask (grow_mask_by base)."));
        advForm->addRow(i18n("Selection grow offset:"), spinSelectionGrowOffset);
        QCheckBox *checkSelectionInvert = new QCheckBox(i18n("Invert selection before creating mask"), diffusionPage);
        checkSelectionInvert->setChecked(selInvert);
        checkSelectionInvert->setToolTip(i18n("Use the inverse of the current selection as the mask (e.g. inpaint outside the selection)."));
        advForm->addRow(QString(), checkSelectionInvert);
        QCheckBox *checkSelectionSquare = new QCheckBox(i18n("Force selection bounds to square"), diffusionPage);
        checkSelectionSquare->setChecked(selSquare);
        checkSelectionSquare->setToolTip(i18n("Use a square area (max of width/height) for generation bounds."));
        advForm->addRow(QString(), checkSelectionSquare);
        diffusionLayout->addWidget(advancedGroup);
        auto saveDiffusionSettings = [sliderSelectionFeather, sliderSelectionBlend, sliderSelectionPadding, checkColorMatch, comboNsfwFilter,
                                       spinSelectionMinTransition, spinSelectionGrowOffset, checkSelectionInvert, checkSelectionSquare]() {
            QJsonObject s = ComfyUIUtils::loadSettingsJson();
            s.insert(QStringLiteral("selection_feather"), sliderSelectionFeather->value());
            s.insert(QStringLiteral("selection_blend"), sliderSelectionBlend->value());
            s.insert(QStringLiteral("selection_padding"), sliderSelectionPadding->value());
            s.insert(QStringLiteral("color_match"), checkColorMatch->isChecked());
            s.insert(QStringLiteral("nsfw_filter"), comboNsfwFilter->currentData().toDouble());
            s.insert(QStringLiteral("selection_min_transition"), spinSelectionMinTransition->value());
            s.insert(QStringLiteral("selection_grow_offset"), spinSelectionGrowOffset->value());
            s.insert(QStringLiteral("selection_invert"), checkSelectionInvert->isChecked());
            s.insert(QStringLiteral("selection_square"), checkSelectionSquare->isChecked());
            ComfyUIUtils::saveSettingsJson(s);
        };
        connect(sliderSelectionFeather, &QSlider::valueChanged, dlg, [labelFeatherVal, saveDiffusionSettings](int v) {
            labelFeatherVal->setText(QStringLiteral("%1 %").arg(v));
            saveDiffusionSettings();
        });
        connect(sliderSelectionBlend, &QSlider::valueChanged, dlg, [labelBlendVal, saveDiffusionSettings](int v) {
            labelBlendVal->setText(QString::number(v) + i18n(" px"));
            saveDiffusionSettings();
        });
        connect(sliderSelectionPadding, &QSlider::valueChanged, dlg, [labelPaddingVal, saveDiffusionSettings](int v) {
            labelPaddingVal->setText(QStringLiteral("%1 %").arg(v));
            saveDiffusionSettings();
        });
        connect(checkColorMatch, &QCheckBox::toggled, dlg, saveDiffusionSettings);
        connect(comboNsfwFilter, QOverload<int>::of(&QComboBox::currentIndexChanged), dlg, [dlg, comboNsfwFilter, saveDiffusionSettings](int idx) {
            // §4.6: First time enabling NSFW filter shows a warning dialog
            if (idx > 0 && !KSharedConfig::openConfig()->group("ComfyUIRemote").readEntry("NsfwFilterWarningShown", false)) {
                QMessageBox::information(dlg, i18n("NSFW Filter"),
                    i18n("The NSFW filter attempts to detect and hide images with explicit content. It may not be fully accurate."));
                KSharedConfig::openConfig()->group("ComfyUIRemote").writeEntry("NsfwFilterWarningShown", true);
            }
            saveDiffusionSettings();
        });
        connect(spinSelectionMinTransition, QOverload<double>::of(&QDoubleSpinBox::valueChanged), dlg, saveDiffusionSettings);
        connect(spinSelectionGrowOffset, QOverload<int>::of(&QSpinBox::valueChanged), dlg, saveDiffusionSettings);
        connect(checkSelectionInvert, &QCheckBox::toggled, dlg, saveDiffusionSettings);
        connect(checkSelectionSquare, &QCheckBox::toggled, dlg, saveDiffusionSettings);
        diffusionLayout->addStretch();
        stack->addWidget(diffusionPage);

        // Interface tab (index 3) – §4.7 Interface Settings
        QWidget *interfacePage = new QWidget(dlg);
        QVBoxLayout *interfaceLayout = new QVBoxLayout(interfacePage);
        QLabel *ifaceHeading = new QLabel(i18n("Interface Settings"), interfacePage);
        QFont ifaceHeadingFont = ifaceHeading->font();
        ifaceHeadingFont.setBold(true);
        ifaceHeading->setFont(ifaceHeadingFont);
        interfaceLayout->addWidget(ifaceHeading);
        QJsonObject ifaceSettings = ComfyUIUtils::loadSettingsJson();
        QFormLayout *ifaceForm = new QFormLayout();
        QComboBox *comboLanguage = new QComboBox(interfacePage);
        const QStringList langNames = { i18n("English"), i18n("Français"), i18n("Deutsch"), i18n("日本語"), i18n("简体中文"), i18n("正體中文"), i18n("한국어"), i18n("Español"), i18n("Italiano"), i18n("Português do Brasil"), i18n("Русский"), i18n("Türkçe"), i18n("Bahasa Indonesia"), i18n("ไทย") };
        const QStringList langCodes = { QStringLiteral("en"), QStringLiteral("fr"), QStringLiteral("de"), QStringLiteral("ja"), QStringLiteral("zh-cn"), QStringLiteral("zh-tw"), QStringLiteral("ko"), QStringLiteral("es"), QStringLiteral("it"), QStringLiteral("pt-br"), QStringLiteral("ru"), QStringLiteral("tr"), QStringLiteral("id"), QStringLiteral("th") };
        for (int i = 0; i < langNames.size(); ++i)
            comboLanguage->addItem(langNames.at(i), langCodes.at(i));
        {
            QString curLang = ifaceSettings.value(QStringLiteral("interface_language")).toString();
            if (curLang.isEmpty()) curLang = QStringLiteral("en");
            int li = comboLanguage->findData(curLang);
            comboLanguage->setCurrentIndex(li >= 0 ? li : 0);
        }
        comboLanguage->setToolTip(i18n("Interface language used by the plugin - requires restart!"));
        ifaceForm->addRow(i18n("Language:"), comboLanguage);
        QComboBox *comboPromptTranslation = new QComboBox(interfacePage);
        comboPromptTranslation->addItem(i18n("Disabled"), QStringLiteral("disabled"));
        for (int i = 0; i < langNames.size(); ++i)
            comboPromptTranslation->addItem(langNames.at(i), langCodes.at(i));
        {
            QString pt = ifaceSettings.value(QStringLiteral("prompt_translation")).toString();
            if (pt.isEmpty()) pt = QStringLiteral("disabled");
            int pti = comboPromptTranslation->findData(pt);
            comboPromptTranslation->setCurrentIndex(pti >= 0 ? pti : 0);
        }
        comboPromptTranslation->setToolTip(i18n("Translate text prompts from the selected language to English."));
        ifaceForm->addRow(i18n("Prompt Translation:"), comboPromptTranslation);
        QSpinBox *spinPromptLines = new QSpinBox(interfacePage);
        spinPromptLines->setRange(1, 10);
        spinPromptLines->setValue(ifaceSettings.value(QStringLiteral("prompt_line_count")).toInt(2));
        spinPromptLines->setToolTip(i18n("Size of the text editor for image descriptions."));
        ifaceForm->addRow(i18n("Prompt Line Count:"), spinPromptLines);
        QSpinBox *spinPromptLinesLive = new QSpinBox(interfacePage);
        spinPromptLinesLive->setRange(1, 10);
        spinPromptLinesLive->setValue(ifaceSettings.value(QStringLiteral("prompt_line_count_live")).toInt(2));
        spinPromptLinesLive->setToolTip(i18n("Height of the prompt editor when the Live workspace is selected (setting prompt_line_count_live)."));
        ifaceForm->addRow(i18n("Prompt line count (Live workspace):"), spinPromptLinesLive);
        QCheckBox *checkPromptResizeHandle = new QCheckBox(i18n("Show prompt resize handles"), interfacePage);
        checkPromptResizeHandle->setChecked(ifaceSettings.value(QStringLiteral("prompt_resize_handle")).toBool(true));
        checkPromptResizeHandle->setToolTip(
            i18n("Show a draggable strip under the prompt and negative prompt editors to resize height without opening settings."));
        ifaceForm->addRow(QString(), checkPromptResizeHandle);
        QSpinBox *spinNegativeLines = new QSpinBox(interfacePage);
        spinNegativeLines->setRange(1, 10);
        spinNegativeLines->setValue(ifaceSettings.value(QStringLiteral("negative_prompt_line_count")).toInt(2));
        spinNegativeLines->setToolTip(i18n("Initial height of the negative prompt editor (1–10 lines)."));
        ifaceForm->addRow(i18n("Negative prompt line count:"), spinNegativeLines);
        QCheckBox *checkShowNegative = new QCheckBox(interfacePage);
        checkShowNegative->setChecked(ifaceSettings.value(QStringLiteral("show_negative_prompt")).toBool(true));
        checkShowNegative->setText(checkShowNegative->isChecked() ? i18n("Hide") : i18n("Show"));
        checkShowNegative->setToolTip(i18n("Show text editor to describe things to avoid."));
        ifaceForm->addRow(i18n("Negative Prompt:"), checkShowNegative);
        connect(checkShowNegative, &QCheckBox::toggled, dlg, [checkShowNegative](bool on) {
            checkShowNegative->setText(on ? i18n("Hide") : i18n("Show"));
        });
        QCheckBox *checkShowSteps = new QCheckBox(interfacePage);
        checkShowSteps->setChecked(ifaceSettings.value(QStringLiteral("show_steps")).toBool(true));
        checkShowSteps->setText(checkShowSteps->isChecked() ? i18n("On") : i18n("Off"));
        checkShowSteps->setToolTip(i18n("Display the number of steps to be evaluated in the weights box."));
        ifaceForm->addRow(i18n("Show Steps:"), checkShowSteps);
        connect(checkShowSteps, &QCheckBox::toggled, dlg, [checkShowSteps](bool on) {
            checkShowSteps->setText(on ? i18n("On") : i18n("Off"));
        });
        // §4.7 / §13.48: Tag auto-completion
        QLabel *tagAutoDesc = new QLabel(i18n("Enable text completion for tags from the selected files."), interfacePage);
        tagAutoDesc->setWordWrap(true);
        ifaceForm->addRow(i18n("Tag Auto-Completion:"), tagAutoDesc);
        QLineEdit *editTagDir = new QLineEdit(interfacePage);
        editTagDir->setClearButtonEnabled(true);
        editTagDir->setText(ifaceSettings.value(QStringLiteral("tag_directory")).toString().trimmed());
        editTagDir->setPlaceholderText(ComfyUIUtils::tagsStorageDir());
        editTagDir->setToolTip(i18n("Folder containing tag CSV files (e.g. Danbooru.csv). Leave empty to use the default plugin tags folder."));
        QPushButton *btnBrowseTagDir = new QPushButton(i18n("Browse…"), interfacePage);
        QHBoxLayout *tagDirLayout = new QHBoxLayout();
        tagDirLayout->addWidget(editTagDir, 1);
        tagDirLayout->addWidget(btnBrowseTagDir);
        QWidget *tagDirRow = new QWidget(interfacePage);
        tagDirRow->setLayout(tagDirLayout);
        ifaceForm->addRow(i18n("Tag CSV folder:"), tagDirRow);
        connect(btnBrowseTagDir, &QPushButton::clicked, dlg, [editTagDir, interfacePage]() {
            const QString start = editTagDir->text().trimmed().isEmpty() ? ComfyUIUtils::tagsStorageDir() : editTagDir->text().trimmed();
            const QString d = QFileDialog::getExistingDirectory(interfacePage, i18n("Tag CSV folder"), start);
            if (!d.isEmpty())
                editTagDir->setText(d);
        });
        QCheckBox *chkTagDanbooru = new QCheckBox(i18n("Danbooru (Danbooru.csv)"), interfacePage);
        QCheckBox *chkTagDanbooruNsfw = new QCheckBox(i18n("Danbooru NSFW"), interfacePage);
        QCheckBox *chkTagE621 = new QCheckBox(i18n("e621 (e621.csv)"), interfacePage);
        QCheckBox *chkTagE621Nsfw = new QCheckBox(i18n("e621 NSFW"), interfacePage);
        {
            QJsonArray tf = ifaceSettings.value(QStringLiteral("tag_files")).toArray();
            QSet<QString> sel;
            for (const QJsonValue &v : tf)
                sel.insert(v.toString());
            if (tf.isEmpty()) {
                sel.insert(QStringLiteral("Danbooru"));
                sel.insert(QStringLiteral("e621"));
            }
            chkTagDanbooru->setChecked(sel.contains(QStringLiteral("Danbooru")));
            chkTagDanbooruNsfw->setChecked(sel.contains(QStringLiteral("Danbooru NSFW")));
            chkTagE621->setChecked(sel.contains(QStringLiteral("e621")));
            chkTagE621Nsfw->setChecked(sel.contains(QStringLiteral("e621 NSFW")));
        }
        QWidget *tagStemCol = new QWidget(interfacePage);
        QVBoxLayout *tagStemLayout = new QVBoxLayout(tagStemCol);
        tagStemLayout->setContentsMargins(0, 0, 0, 0);
        tagStemLayout->addWidget(chkTagDanbooru);
        tagStemLayout->addWidget(chkTagDanbooruNsfw);
        tagStemLayout->addWidget(chkTagE621);
        tagStemLayout->addWidget(chkTagE621Nsfw);
        ifaceForm->addRow(i18n("Tag lists:"), tagStemCol);
        QPushButton *btnLookNewTagFiles = new QPushButton(i18n("Look for new tag files"), interfacePage);
        QPushButton *btnOpenTagFolder = new QPushButton(i18n("Open folder where custom tag files can be placed"), interfacePage);
        btnLookNewTagFiles->setToolTip(i18n("Reload tag lists from disk (e.g. after adding CSV files)."));
        btnOpenTagFolder->setToolTip(i18n("Open the tag folder in the file manager."));
        QHBoxLayout *tagActionLayout = new QHBoxLayout();
        tagActionLayout->addWidget(btnLookNewTagFiles);
        tagActionLayout->addWidget(btnOpenTagFolder);
        tagActionLayout->addStretch();
        QWidget *tagActionRow = new QWidget(interfacePage);
        tagActionRow->setLayout(tagActionLayout);
        ifaceForm->addRow(QString(), tagActionRow);
        connect(btnLookNewTagFiles, &QPushButton::clicked, this, [this]() { refreshPromptTagCompleter(); });
        connect(btnOpenTagFolder, &QPushButton::clicked, dlg, [editTagDir]() {
            const QString dir = editTagDir->text().trimmed().isEmpty() ? ComfyUIUtils::tagsStorageDir() : editTagDir->text().trimmed();
            QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
        });
        QLabel *tagFileHint = new QLabel(
            i18n("CSV must include a “tag” column (see spec §13.215). In prompts, press Ctrl+Space to complete the word at the cursor."),
            interfacePage);
        tagFileHint->setWordWrap(true);
        ifaceForm->addRow(QString(), tagFileHint);
        // §4.7: order — Finished Generation and Apply before Save Image Format
        QComboBox *comboFinishedAction = new QComboBox(interfacePage);
        comboFinishedAction->addItem(i18n("Do Nothing"), QStringLiteral("none"));
        comboFinishedAction->addItem(i18n("Preview"), QStringLiteral("preview"));
        comboFinishedAction->addItem(i18n("Apply"), QStringLiteral("apply"));
        QString finAction = ifaceSettings.value(QStringLiteral("generation_finished_action")).toString();
        if (finAction.isEmpty()) finAction = QStringLiteral("preview");
        int finIdx = comboFinishedAction->findData(finAction);
        comboFinishedAction->setCurrentIndex(finIdx >= 0 ? finIdx : 1);
        comboFinishedAction->setToolTip(i18n("Action to take when an image generation job finishes."));
        ifaceForm->addRow(i18n("Finished Generation:"), comboFinishedAction);
        QComboBox *comboApplyBehavior = new QComboBox(interfacePage);
        comboApplyBehavior->addItem(i18n("Modify active layer"), QStringLiteral("replace"));
        comboApplyBehavior->addItem(i18n("New layer on top"), QStringLiteral("layer"));
        comboApplyBehavior->addItem(i18n("New layer above active"), QStringLiteral("layer_active"));
        QString applyBeh = ifaceSettings.value(QStringLiteral("apply_behavior")).toString();
        if (applyBeh.isEmpty()) applyBeh = QStringLiteral("layer");
        int applyIdx = comboApplyBehavior->findData(applyBeh);
        comboApplyBehavior->setCurrentIndex(applyIdx >= 0 ? applyIdx : 1);
        comboApplyBehavior->setToolTip(i18n("Choose how result images are applied to the canvas (generation workspaces)."));
        ifaceForm->addRow(i18n("Apply Behavior:"), comboApplyBehavior);
        QComboBox *comboApplyBehaviorLive = new QComboBox(interfacePage);
        comboApplyBehaviorLive->addItem(i18n("Modify active layer"), QStringLiteral("replace"));
        comboApplyBehaviorLive->addItem(i18n("New layer on top"), QStringLiteral("layer"));
        comboApplyBehaviorLive->addItem(i18n("New layer above active"), QStringLiteral("layer_active"));
        QString applyBehLive = ifaceSettings.value(QStringLiteral("apply_behavior_live")).toString();
        if (applyBehLive.isEmpty()) applyBehLive = QStringLiteral("replace");
        int applyLiveIdx = comboApplyBehaviorLive->findData(applyBehLive);
        comboApplyBehaviorLive->setCurrentIndex(applyLiveIdx >= 0 ? applyLiveIdx : 0);
        comboApplyBehaviorLive->setToolTip(i18n("Choose how result images are applied to the canvas in Live mode."));
        ifaceForm->addRow(i18n("Apply Behavior (Live):"), comboApplyBehaviorLive);
        QCheckBox *checkNewSeedAfterApply = new QCheckBox(interfacePage);
        checkNewSeedAfterApply->setChecked(ifaceSettings.value(QStringLiteral("new_seed_after_apply")).toBool(false));
        checkNewSeedAfterApply->setToolTip(i18n("Pick a new seed after copying the result to the canvas in Live mode."));
        ifaceForm->addRow(i18n("Live: New Seed after Apply:"), checkNewSeedAfterApply);
        QComboBox *comboSaveFormat = new QComboBox(interfacePage);
        comboSaveFormat->addItem(i18n("PNG (fast)"), QStringLiteral("png_small"));
        comboSaveFormat->addItem(i18n("PNG"), QStringLiteral("png"));
        comboSaveFormat->addItem(i18n("WebP"), QStringLiteral("webp"));
        comboSaveFormat->addItem(i18n("WebP (lossless)"), QStringLiteral("webp_lossless"));
        comboSaveFormat->addItem(i18n("JPEG"), QStringLiteral("jpeg"));
        {
            QString sf = ifaceSettings.value(QStringLiteral("save_image_format")).toString();
            if (sf.isEmpty()) sf = QStringLiteral("png");
            int sfi = comboSaveFormat->findData(sf);
            if (sfi < 0 && sf == QLatin1String("png_small")) sfi = 0;
            comboSaveFormat->setCurrentIndex(sfi >= 0 ? sfi : 1);
        }
        comboSaveFormat->setToolTip(i18n("File format for saved images from thumbnails."));
        ifaceForm->addRow(i18n("Save Image Format:"), comboSaveFormat);
        QSpinBox *spinSaveJpegQuality = new QSpinBox(interfacePage);
        spinSaveJpegQuality->setRange(0, 100);
        spinSaveJpegQuality->setValue(ComfyUIUtils::saveImageQualityJpeg(ifaceSettings));
        spinSaveJpegQuality->setToolTip(i18n("JPEG quality when saving from history (setting save_image_quality_jpeg, default 85)."));
        ifaceForm->addRow(i18n("JPEG save quality:"), spinSaveJpegQuality);
        QSpinBox *spinSaveWebpQuality = new QSpinBox(interfacePage);
        spinSaveWebpQuality->setRange(0, 100);
        spinSaveWebpQuality->setValue(ComfyUIUtils::saveImageQualityWebp(ifaceSettings));
        spinSaveWebpQuality->setToolTip(i18n("WebP quality for lossy WebP when saving from history (setting save_image_quality_webp, default 80)."));
        ifaceForm->addRow(i18n("WebP save quality (lossy):"), spinSaveWebpQuality);
        QCheckBox *checkSaveMeta = new QCheckBox(i18n("Save Image Metadata"), interfacePage);
        checkSaveMeta->setChecked(ifaceSettings.value(QStringLiteral("save_image_metadata")).toBool(true));
        checkSaveMeta->setToolTip(i18n("When saving generated images from thumbnails, include metadata in the PNG."));
        ifaceForm->addRow(QString(), checkSaveMeta);
        QLineEdit *editSaveImageNameFormat = new QLineEdit(interfacePage);
        editSaveImageNameFormat->setText(ifaceSettings.value(QStringLiteral("save_image_file_name_format")).toString());
        editSaveImageNameFormat->setPlaceholderText(
            QStringLiteral("{document_name}-generated-{job_timestamp}-{job_index}-{prompt}"));
        editSaveImageNameFormat->setToolTip(
            i18n("Default suggested filename when saving from history. Placeholders: {document_name}, {job_timestamp}, "
                 "{job_index} (1-based), {prompt}. Leave empty for the default template."));
        ifaceForm->addRow(i18n("Save filename template:"), editSaveImageNameFormat);
        QCheckBox *checkDumpWorkflow = new QCheckBox(i18n("Dump Workflow"), interfacePage);
        checkDumpWorkflow->setChecked(ifaceSettings.value(QStringLiteral("dump_workflow")).toBool(false));
        checkDumpWorkflow->setToolTip(i18n("Write latest ComfyUI prompt to the log folder for test & debug."));
        ifaceForm->addRow(QString(), checkDumpWorkflow);
        interfaceLayout->addLayout(ifaceForm);
        m_d->checkConfirmDiscardImage = new QCheckBox(i18n("Ask for confirmation when discarding an image from history"), interfacePage);
        m_d->checkConfirmDiscardImage->setChecked(KSharedConfig::openConfig()->group("ComfyUIRemote").readEntry("ConfirmDiscardImage", true));
        connect(m_d->checkConfirmDiscardImage, &QCheckBox::toggled, this, [this](bool checked) {
            KSharedConfig::openConfig()->group("ComfyUIRemote").writeEntry("ConfirmDiscardImage", checked);
        });
        interfaceLayout->addWidget(m_d->checkConfirmDiscardImage);
        // §4.7 / §13.184: Apply region result
        QComboBox *comboApplyRegionBehavior = new QComboBox(interfacePage);
        comboApplyRegionBehavior->addItem(i18n("None"), QStringLiteral("none"));
        comboApplyRegionBehavior->addItem(i18n("Replace (modify region layer in place)"), QStringLiteral("replace"));
        comboApplyRegionBehavior->addItem(i18n("Layer group (new group with result layer inside)"), QStringLiteral("layer_group"));
        comboApplyRegionBehavior->addItem(i18n("Transparency mask (apply as mask on region layer)"), QStringLiteral("transparency_mask"));
        comboApplyRegionBehavior->addItem(i18n("No hide (new layer; keep sibling layers visible)"), QStringLiteral("no_hide"));
        QString savedRegionBehavior = ifaceSettings.value(QStringLiteral("apply_region_behavior")).toString();
        if (savedRegionBehavior.isEmpty()) savedRegionBehavior = QStringLiteral("layer_group");
        int regionIdx = comboApplyRegionBehavior->findData(savedRegionBehavior);
        comboApplyRegionBehavior->setCurrentIndex(regionIdx >= 0 ? regionIdx : 1);
        comboApplyRegionBehavior->setToolTip(i18n("When applying a result that was generated with regions, how to place the result per region (create_result_layer)."));
        interfaceLayout->addWidget(new QLabel(i18n("Apply region result:"), interfacePage));
        interfaceLayout->addWidget(comboApplyRegionBehavior);
        QComboBox *comboApplyRegionBehaviorLive = new QComboBox(interfacePage);
        comboApplyRegionBehaviorLive->addItem(i18n("None"), QStringLiteral("none"));
        comboApplyRegionBehaviorLive->addItem(i18n("Replace (modify region layer in place)"), QStringLiteral("replace"));
        comboApplyRegionBehaviorLive->addItem(i18n("Layer group (new group with result layer inside)"), QStringLiteral("layer_group"));
        comboApplyRegionBehaviorLive->addItem(i18n("Transparency mask (apply as mask on region layer)"), QStringLiteral("transparency_mask"));
        comboApplyRegionBehaviorLive->addItem(i18n("No hide (new layer; keep sibling layers visible)"), QStringLiteral("no_hide"));
        QString savedRegionBehaviorLive = ifaceSettings.value(QStringLiteral("apply_region_behavior_live")).toString();
        if (savedRegionBehaviorLive.isEmpty()) savedRegionBehaviorLive = QStringLiteral("replace");
        int regionLiveIdx = comboApplyRegionBehaviorLive->findData(savedRegionBehaviorLive);
        comboApplyRegionBehaviorLive->setCurrentIndex(regionLiveIdx >= 0 ? regionLiveIdx : 1);
        comboApplyRegionBehaviorLive->setToolTip(
            i18n("Same as “Apply region result”, used when the Live workspace is active while you apply a region job from history."));
        interfaceLayout->addWidget(new QLabel(i18n("Apply region result (Live workspace):"), interfacePage));
        interfaceLayout->addWidget(comboApplyRegionBehaviorLive);
        auto updateSaveFormatSideEffects = [checkSaveMeta, comboSaveFormat, spinSaveJpegQuality, spinSaveWebpQuality]() {
            const QString f = comboSaveFormat->currentData().toString();
            checkSaveMeta->setEnabled(f == QLatin1String("png") || f == QLatin1String("png_small"));
            spinSaveJpegQuality->setEnabled(f == QLatin1String("jpeg"));
            spinSaveWebpQuality->setEnabled(f == QLatin1String("webp"));
        };
        updateSaveFormatSideEffects();
        auto saveIfaceSettings = [this, comboFinishedAction, comboApplyBehavior, comboApplyBehaviorLive, checkNewSeedAfterApply, comboApplyRegionBehavior,
                                   comboApplyRegionBehaviorLive, comboLanguage, comboPromptTranslation, spinPromptLines, spinPromptLinesLive,
                                   checkPromptResizeHandle, spinNegativeLines, checkShowNegative, checkShowSteps, comboSaveFormat, checkSaveMeta,
                                   spinSaveJpegQuality, spinSaveWebpQuality, editSaveImageNameFormat, checkDumpWorkflow, editTagDir, chkTagDanbooru,
                                   chkTagDanbooruNsfw, chkTagE621, chkTagE621Nsfw]() {
            QJsonObject s = ComfyUIUtils::loadSettingsJson();
            s.insert(QStringLiteral("interface_language"), comboLanguage->currentData().toString());
            s.insert(QStringLiteral("prompt_translation"), comboPromptTranslation->currentData().toString());
            s.insert(QStringLiteral("prompt_line_count"), spinPromptLines->value());
            s.insert(QStringLiteral("prompt_line_count_live"), spinPromptLinesLive->value());
            s.insert(QStringLiteral("prompt_resize_handle"), checkPromptResizeHandle->isChecked());
            s.insert(QStringLiteral("negative_prompt_line_count"), spinNegativeLines->value());
            s.insert(QStringLiteral("show_negative_prompt"), checkShowNegative->isChecked());
            s.insert(QStringLiteral("show_steps"), checkShowSteps->isChecked());
            s.insert(QStringLiteral("save_image_file_name_format"), editSaveImageNameFormat->text().trimmed());
            s.insert(QStringLiteral("tag_directory"), editTagDir->text().trimmed());
            QJsonArray tagFiles;
            if (chkTagDanbooru->isChecked())
                tagFiles.append(QStringLiteral("Danbooru"));
            if (chkTagDanbooruNsfw->isChecked())
                tagFiles.append(QStringLiteral("Danbooru NSFW"));
            if (chkTagE621->isChecked())
                tagFiles.append(QStringLiteral("e621"));
            if (chkTagE621Nsfw->isChecked())
                tagFiles.append(QStringLiteral("e621 NSFW"));
            s.insert(QStringLiteral("tag_files"), tagFiles);
            s.insert(QStringLiteral("save_image_format"), comboSaveFormat->currentData().toString());
            s.insert(QStringLiteral("save_image_quality_jpeg"), spinSaveJpegQuality->value());
            s.insert(QStringLiteral("save_image_quality_webp"), spinSaveWebpQuality->value());
            s.insert(QStringLiteral("save_image_metadata"), checkSaveMeta->isChecked());
            s.insert(QStringLiteral("dump_workflow"), checkDumpWorkflow->isChecked());
            s.insert(QStringLiteral("generation_finished_action"), comboFinishedAction->currentData().toString());
            s.insert(QStringLiteral("apply_behavior"), comboApplyBehavior->currentData().toString());
            s.insert(QStringLiteral("apply_behavior_live"), comboApplyBehaviorLive->currentData().toString());
            s.insert(QStringLiteral("new_seed_after_apply"), checkNewSeedAfterApply->isChecked());
            s.insert(QStringLiteral("apply_region_behavior"), comboApplyRegionBehavior->currentData().toString());
            s.insert(QStringLiteral("apply_region_behavior_live"), comboApplyRegionBehaviorLive->currentData().toString());
            ComfyUIUtils::saveSettingsJson(s);
            applyInterfaceAppearanceSettings();
            refreshPromptTagCompleter();
            persistDocumentDefaultsToSettings();  // §13.194: translation fields in document_defaults
        };
        connect(comboLanguage, QOverload<int>::of(&QComboBox::currentIndexChanged), dlg, saveIfaceSettings);
        connect(comboPromptTranslation, QOverload<int>::of(&QComboBox::currentIndexChanged), dlg, saveIfaceSettings);
        connect(spinPromptLines, QOverload<int>::of(&QSpinBox::valueChanged), dlg, saveIfaceSettings);
        connect(spinPromptLinesLive, QOverload<int>::of(&QSpinBox::valueChanged), dlg, saveIfaceSettings);
        connect(checkPromptResizeHandle, &QCheckBox::toggled, dlg, saveIfaceSettings);
        connect(spinNegativeLines, QOverload<int>::of(&QSpinBox::valueChanged), dlg, saveIfaceSettings);
        connect(checkShowNegative, &QCheckBox::toggled, dlg, saveIfaceSettings);
        connect(checkShowSteps, &QCheckBox::toggled, dlg, saveIfaceSettings);
        connect(editTagDir, &QLineEdit::textChanged, dlg, saveIfaceSettings);
        connect(chkTagDanbooru, &QCheckBox::toggled, dlg, saveIfaceSettings);
        connect(chkTagDanbooruNsfw, &QCheckBox::toggled, dlg, saveIfaceSettings);
        connect(chkTagE621, &QCheckBox::toggled, dlg, saveIfaceSettings);
        connect(chkTagE621Nsfw, &QCheckBox::toggled, dlg, saveIfaceSettings);
        connect(comboSaveFormat, QOverload<int>::of(&QComboBox::currentIndexChanged), dlg, [saveIfaceSettings, updateSaveFormatSideEffects](int) {
            updateSaveFormatSideEffects();
            saveIfaceSettings();
        });
        connect(spinSaveJpegQuality, QOverload<int>::of(&QSpinBox::valueChanged), dlg, saveIfaceSettings);
        connect(spinSaveWebpQuality, QOverload<int>::of(&QSpinBox::valueChanged), dlg, saveIfaceSettings);
        connect(checkSaveMeta, &QCheckBox::toggled, dlg, saveIfaceSettings);
        connect(editSaveImageNameFormat, &QLineEdit::textChanged, dlg, saveIfaceSettings);
        connect(checkDumpWorkflow, &QCheckBox::toggled, dlg, saveIfaceSettings);
        connect(comboFinishedAction, QOverload<int>::of(&QComboBox::currentIndexChanged), dlg, saveIfaceSettings);
        connect(comboApplyBehavior, QOverload<int>::of(&QComboBox::currentIndexChanged), dlg, saveIfaceSettings);
        connect(comboApplyBehaviorLive, QOverload<int>::of(&QComboBox::currentIndexChanged), dlg, saveIfaceSettings);
        connect(checkNewSeedAfterApply, &QCheckBox::toggled, dlg, saveIfaceSettings);
        connect(comboApplyRegionBehavior, QOverload<int>::of(&QComboBox::currentIndexChanged), dlg, saveIfaceSettings);
        connect(comboApplyRegionBehaviorLive, QOverload<int>::of(&QComboBox::currentIndexChanged), dlg, saveIfaceSettings);
        interfaceLayout->addStretch();
        stack->addWidget(interfacePage);

        // Performance tab (index 4) — §4.8 Performance Settings
        QWidget *perfPage = new QWidget(dlg);
        QVBoxLayout *perfLayout = new QVBoxLayout(perfPage);
        QLabel *perfHeading = new QLabel(i18n("Performance Settings"), perfPage);
        QFont perfHeadingFont = perfHeading->font();
        perfHeadingFont.setBold(true);
        perfHeading->setFont(perfHeadingFont);
        perfLayout->addWidget(perfHeading);
        QFormLayout *perfForm = new QFormLayout();
        QLabel *activeHistDesc = new QLabel(i18n("Main memory (RAM) used for the history of generated images."), perfPage);
        activeHistDesc->setWordWrap(true);
        m_d->labelHistoryUsageMb = new QLabel(i18n("Currently using %1 MB", QStringLiteral("0.0")), perfPage);
        m_d->labelHistoryUsageMb->setStyleSheet(QStringLiteral("color: green;"));
        m_d->labelHistoryUsageMb->setToolTip(i18n("Total size of result images in the session history cache."));
        QJsonObject perfSettings = ComfyUIUtils::loadSettingsJson();
        QSpinBox *spinActiveHistoryMb = new QSpinBox(perfPage);
        spinActiveHistoryMb->setRange(5, 20000);
        spinActiveHistoryMb->setSingleStep(100);
        spinActiveHistoryMb->setSuffix(i18n(" MB"));
        {
            int amb = perfSettings.value(QStringLiteral("history_active_mb")).toInt(0);
            if (amb <= 0) amb = perfSettings.value(QStringLiteral("history_storage")).toInt(20);
            spinActiveHistoryMb->setValue(qBound(5, amb, 20000));
        }
        spinActiveHistoryMb->setToolTip(i18n("Oldest history entries are removed when over this limit."));
        QHBoxLayout *activeHistRow = new QHBoxLayout();
        activeHistRow->addWidget(spinActiveHistoryMb);
        activeHistRow->addWidget(m_d->labelHistoryUsageMb, 1);
        QWidget *activeHistWrap = new QWidget(perfPage);
        QVBoxLayout *activeHistVBox = new QVBoxLayout(activeHistWrap);
        activeHistVBox->setContentsMargins(0, 0, 0, 0);
        activeHistVBox->addWidget(activeHistDesc);
        activeHistVBox->addLayout(activeHistRow);
        perfForm->addRow(i18n("Active History Size:"), activeHistWrap);
        QLabel *storedHistDesc = new QLabel(i18n("Memory used to store generated images in .kra files on disk."), perfPage);
        storedHistDesc->setWordWrap(true);
        QLabel *labelStoredKraMb = new QLabel(perfPage);
        labelStoredKraMb->setStyleSheet(QStringLiteral("color: green;"));
        m_d->labelStoredHistoryMb = labelStoredKraMb;
        labelStoredKraMb->setText(i18n("Currently using %1 MB", QStringLiteral("0.0")));
        QSpinBox *spinStoredHistoryMb = new QSpinBox(perfPage);
        spinStoredHistoryMb->setRange(5, 2000);
        spinStoredHistoryMb->setSingleStep(5);
        spinStoredHistoryMb->setSuffix(i18n(" MB"));
        spinStoredHistoryMb->setValue(qBound(5, perfSettings.value(QStringLiteral("history_document_storage_mb")).toInt(20), 2000));
        spinStoredHistoryMb->setToolTip(i18n("Reserved for document-embedded history quota (parity with Python plugin)."));
        QHBoxLayout *storedHistRow = new QHBoxLayout();
        storedHistRow->addWidget(spinStoredHistoryMb);
        storedHistRow->addWidget(labelStoredKraMb, 1);
        QWidget *storedHistWrap = new QWidget(perfPage);
        QVBoxLayout *storedHistVBox = new QVBoxLayout(storedHistWrap);
        storedHistVBox->setContentsMargins(0, 0, 0, 0);
        storedHistVBox->addWidget(storedHistDesc);
        storedHistVBox->addLayout(storedHistRow);
        perfForm->addRow(i18n("Stored History Size:"), storedHistWrap);
        QLabel *perfPresetDesc = new QLabel(
            i18n("Configures performance settings to match available hardware."), perfPage);
        perfPresetDesc->setWordWrap(true);
        m_d->labelPerfDevice = new QLabel(perfPage);
        m_d->labelPerfDevice->setWordWrap(true);
        m_d->labelPerfDevice->setText(m_d->comfyDeviceSummary.isEmpty()
                                          ? i18n("Device: (connect to server)")
                                          : m_d->comfyDeviceSummary);
        QComboBox *comboPerfPreset = new QComboBox(perfPage);
        comboPerfPreset->addItem(i18n("Automatic"), QStringLiteral("auto"));
        comboPerfPreset->addItem(i18n("CPU"), QStringLiteral("cpu"));
        comboPerfPreset->addItem(i18n("GPU low (up to 6GB)"), QStringLiteral("low"));
        comboPerfPreset->addItem(i18n("GPU medium (6GB to 12GB)"), QStringLiteral("medium"));
        comboPerfPreset->addItem(i18n("GPU high (more than 12GB)"), QStringLiteral("high"));
        comboPerfPreset->addItem(i18n("Cloud"), QStringLiteral("cloud"));
        comboPerfPreset->addItem(i18n("Custom"), QStringLiteral("custom"));
        QString pp = perfSettings.value(QStringLiteral("performance_preset")).toString();
        if (pp.isEmpty()) pp = QStringLiteral("auto");
        int ppi = comboPerfPreset->findData(pp);
        comboPerfPreset->setCurrentIndex(ppi >= 0 ? ppi : 0);
        comboPerfPreset->setToolTip(i18n("Configures performance settings to match available hardware."));
        QComboBox *comboDiffusionScaleMode = new QComboBox(perfPage);
        comboDiffusionScaleMode->addItem(i18n("Resize (default)"), QStringLiteral("resize"));
        comboDiffusionScaleMode->addItem(i18n("None — no performance resolution scaling"), QStringLiteral("none"));
        comboDiffusionScaleMode->addItem(i18n("Upscale small (under ~1.5×)"), QStringLiteral("upscale_small"));
        comboDiffusionScaleMode->addItem(i18n("Upscale fast"), QStringLiteral("upscale_fast"));
        comboDiffusionScaleMode->addItem(i18n("Upscale quality"), QStringLiteral("upscale_quality"));
        comboDiffusionScaleMode->setToolTip(
            i18n("Controls whether the performance preset applies a resolution multiplier to generation, and selects the "
                 "ImageScale interpolation used in the Upscale workspace. "
                 "\"Upscale small\" caps the multiplier at 1.5× (light upscale)."));
        {
            QString dsm = perfSettings.value(QStringLiteral("diffusion_scale_mode")).toString();
            if (dsm.isEmpty())
                dsm = QStringLiteral("resize");
            const int dsIx = comboDiffusionScaleMode->findData(dsm);
            comboDiffusionScaleMode->setCurrentIndex(dsIx >= 0 ? dsIx : 0);
        }
        perfForm->addRow(QString(), perfPresetDesc);
        perfForm->addRow(QString(), m_d->labelPerfDevice);
        perfForm->addRow(i18n("Performance Preset:"), comboPerfPreset);
        perfForm->addRow(i18n("Diffusion input scale:"), comboDiffusionScaleMode);
        QSpinBox *spinUpscaleTileExtent = new QSpinBox(perfPage);
        spinUpscaleTileExtent->setRange(256, 2048);
        spinUpscaleTileExtent->setSingleStep(64);
        spinUpscaleTileExtent->setSuffix(i18n(" px"));
        spinUpscaleTileExtent->setValue(
            qBound(256, perfSettings.value(QStringLiteral("upscale_tile_estimate_extent")).toInt(512), 2048));
        spinUpscaleTileExtent->setToolTip(
            i18n("Tile size used to estimate how many tiles a large upscaled output would need (memory planning). "
                 "The built-in upscale workflow is still a single ImageScale pass."));
        perfForm->addRow(i18n("Upscale tile estimate:"), spinUpscaleTileExtent);
        QWidget *customPerfWidget = new QWidget(perfPage);
        QFormLayout *customPerfForm = new QFormLayout(customPerfWidget);
        customPerfForm->setContentsMargins(0, 0, 0, 0);
        QSlider *sliderPerfBatch = new QSlider(Qt::Horizontal, customPerfWidget);
        sliderPerfBatch->setRange(1, 16);
        QLabel *labelPerfBatchVal = new QLabel(QStringLiteral("1"), customPerfWidget);
        QHBoxLayout *batchRow = new QHBoxLayout();
        batchRow->addWidget(sliderPerfBatch);
        batchRow->addWidget(labelPerfBatchVal);
        sliderPerfBatch->setToolTip(i18n("Increase efficiency by generating multiple images at once."));
        customPerfForm->addRow(i18n("Maximum Batch Size:"), batchRow);
        QSlider *sliderPerfRes = new QSlider(Qt::Horizontal, customPerfWidget);
        sliderPerfRes->setRange(3, 15);
        QLabel *labelPerfResVal = new QLabel(QStringLiteral("1.0×"), customPerfWidget);
        QHBoxLayout *resRow = new QHBoxLayout();
        resRow->addWidget(sliderPerfRes);
        resRow->addWidget(labelPerfResVal);
        sliderPerfRes->setToolTip(
            i18n("Scaling factor for generation. Values below 1.0 improve performance for high resolution canvas."));
        customPerfForm->addRow(i18n("Resolution Multiplier:"), resRow);
        QSpinBox *spinMaxMp = new QSpinBox(customPerfWidget);
        spinMaxMp->setRange(1, 99);
        spinMaxMp->setSuffix(i18n(" MP"));
        spinMaxMp->setValue(qBound(1, perfSettings.value(QStringLiteral("max_pixel_count_mp")).toInt(8), 99));
        spinMaxMp->setToolTip(i18n("Maximum resolution to generate images at, in megapixels (FullHD ~ 2MP, 4k ~ 8MP)."));
        QCheckBox *checkMaxMpAuto = new QCheckBox(i18n("Automatic"), customPerfWidget);
        checkMaxMpAuto->setToolTip(i18n("When enabled, megapixel limit follows the performance preset or server defaults."));
        const bool maxMpAutoInit = perfSettings.value(QStringLiteral("max_pixel_auto")).toBool(true);
        checkMaxMpAuto->setChecked(maxMpAutoInit);
        spinMaxMp->setEnabled(!maxMpAutoInit);
        QHBoxLayout *maxMpRow = new QHBoxLayout();
        maxMpRow->addWidget(spinMaxMp);
        maxMpRow->addWidget(checkMaxMpAuto);
        maxMpRow->addStretch();
        customPerfForm->addRow(i18n("Maximum Pixel Count:"), maxMpRow);
        QWidget *tiledVaeWidget = new QWidget(customPerfWidget);
        QHBoxLayout *tiledVaeLay = new QHBoxLayout(tiledVaeWidget);
        tiledVaeLay->setContentsMargins(0, 0, 0, 0);
        QRadioButton *radioTiledAutomatic = new QRadioButton(i18n("Automatic"), tiledVaeWidget);
        QRadioButton *radioTiledAlways = new QRadioButton(i18n("Always"), tiledVaeWidget);
        auto *bgTiledVae = new QButtonGroup(tiledVaeWidget);
        bgTiledVae->addButton(radioTiledAutomatic);
        bgTiledVae->addButton(radioTiledAlways);
        tiledVaeWidget->setToolTip(i18n("Conserve memory by processing output images in smaller tiles."));
        tiledVaeLay->addWidget(radioTiledAutomatic);
        tiledVaeLay->addWidget(radioTiledAlways);
        tiledVaeLay->addStretch();
        {
            QString tvm = perfSettings.value(QStringLiteral("tiled_vae_mode")).toString();
            if (tvm.isEmpty())
                tvm = perfSettings.value(QStringLiteral("tiled_vae_always")).toBool(false) ? QStringLiteral("always")
                                                                                          : QStringLiteral("automatic");
            if (tvm == QLatin1String("always"))
                radioTiledAlways->setChecked(true);
            else
                radioTiledAutomatic->setChecked(true);
        }
        customPerfForm->addRow(i18n("Tiled VAE:"), tiledVaeWidget);
        QCheckBox *checkDynCache = new QCheckBox(i18n("Dynamic Caching"), customPerfWidget);
        checkDynCache->setChecked(perfSettings.value(QStringLiteral("dynamic_caching")).toBool(false));
        checkDynCache->setToolTip(
            i18n("Re-use outputs of previous steps (First Block Cache) to speed up generation.\n\n"
                 "When enabled, the dock turns on common enable toggles on workflow nodes whose class names look like "
                 "First Block / Block / FB / Tea cache nodes (if those nodes are already in the graph)."));
        customPerfForm->addRow(QString(), checkDynCache);
        QCheckBox *checkMultiThread = new QCheckBox(i18n("Multi-Threading"), customPerfWidget);
        checkMultiThread->setChecked(perfSettings.value(QStringLiteral("multi_threading")).toBool(true));
        checkMultiThread->setToolTip(
            i18n("Perform certain plugin operations in background threads.\n\n"
                 "When enabled: Import Workflow reads and strips comments off the UI thread, and large \"Dump workflow\" "
                 "writes to last_comfy_prompt.json are written on a worker thread."));
        customPerfForm->addRow(QString(), checkMultiThread);
        auto syncPerfSlidersFromDock = [this, sliderPerfBatch, labelPerfBatchVal, sliderPerfRes, labelPerfResVal, spinMaxMp,
                                        checkMaxMpAuto, radioTiledAutomatic, radioTiledAlways, comboDiffusionScaleMode,
                                        spinUpscaleTileExtent]() {
            if (m_d->spinBatchCount) {
                sliderPerfBatch->blockSignals(true);
                sliderPerfBatch->setValue(qBound(1, m_d->spinBatchCount->value(), 16));
                sliderPerfBatch->blockSignals(false);
                labelPerfBatchVal->setText(QString::number(sliderPerfBatch->value()));
            }
            if (m_d->sliderResolutionMultiplier) {
                sliderPerfRes->blockSignals(true);
                sliderPerfRes->setValue(m_d->sliderResolutionMultiplier->value());
                sliderPerfRes->blockSignals(false);
                labelPerfResVal->setText(QString::number(m_d->resolutionMultiplier, 'f', 1) + QLatin1String("×"));
            }
            const QJsonObject s = ComfyUIUtils::loadSettingsJson();
            spinMaxMp->blockSignals(true);
            spinMaxMp->setValue(qBound(1, s.value(QStringLiteral("max_pixel_count_mp")).toInt(8), 99));
            spinMaxMp->blockSignals(false);
            const bool autoMp = s.value(QStringLiteral("max_pixel_auto")).toBool(true);
            checkMaxMpAuto->blockSignals(true);
            checkMaxMpAuto->setChecked(autoMp);
            checkMaxMpAuto->blockSignals(false);
            spinMaxMp->setEnabled(!autoMp);
            QString tvm = s.value(QStringLiteral("tiled_vae_mode")).toString();
            if (tvm.isEmpty())
                tvm = s.value(QStringLiteral("tiled_vae_always")).toBool(false) ? QStringLiteral("always")
                                                                              : QStringLiteral("automatic");
            radioTiledAutomatic->blockSignals(true);
            radioTiledAlways->blockSignals(true);
            if (tvm == QLatin1String("always"))
                radioTiledAlways->setChecked(true);
            else
                radioTiledAutomatic->setChecked(true);
            radioTiledAutomatic->blockSignals(false);
            radioTiledAlways->blockSignals(false);
            QString dsm2 = s.value(QStringLiteral("diffusion_scale_mode")).toString();
            if (dsm2.isEmpty())
                dsm2 = QStringLiteral("resize");
            const int dsmIx = comboDiffusionScaleMode->findData(dsm2);
            comboDiffusionScaleMode->blockSignals(true);
            comboDiffusionScaleMode->setCurrentIndex(dsmIx >= 0 ? dsmIx : 0);
            comboDiffusionScaleMode->blockSignals(false);
            spinUpscaleTileExtent->blockSignals(true);
            spinUpscaleTileExtent->setValue(
                qBound(256, s.value(QStringLiteral("upscale_tile_estimate_extent")).toInt(512), 2048));
            spinUpscaleTileExtent->blockSignals(false);
            if (m_d->labelPerfDevice)
                m_d->labelPerfDevice->setText(m_d->comfyDeviceSummary.isEmpty()
                                                  ? i18n("Device: (connect to server)")
                                                  : m_d->comfyDeviceSummary);
        };
        syncPerfSlidersFromDock();
        auto updateCustomPerfVisible = [comboPerfPreset, customPerfWidget]() {
            customPerfWidget->setVisible(comboPerfPreset->currentData().toString() == QLatin1String("custom"));
        };
        updateCustomPerfVisible();
        auto savePerfSettings = [this, comboPerfPreset, comboDiffusionScaleMode, spinUpscaleTileExtent, spinActiveHistoryMb,
                                 spinStoredHistoryMb, spinMaxMp, checkMaxMpAuto, radioTiledAlways, checkDynCache,
                                 checkMultiThread]() {
            QJsonObject s = ComfyUIUtils::loadSettingsJson();
            s.insert(QStringLiteral("performance_preset"), comboPerfPreset->currentData().toString());
            {
                QString dsm = comboDiffusionScaleMode->currentData().toString();
                if (dsm.isEmpty())
                    dsm = QStringLiteral("resize");
                s.insert(QStringLiteral("diffusion_scale_mode"), dsm);
            }
            s.insert(QStringLiteral("upscale_tile_estimate_extent"), spinUpscaleTileExtent->value());
            s.insert(QStringLiteral("history_active_mb"), spinActiveHistoryMb->value());
            s.insert(QStringLiteral("history_document_storage_mb"), spinStoredHistoryMb->value());
            s.insert(QStringLiteral("max_pixel_count_mp"), spinMaxMp->value());
            s.insert(QStringLiteral("max_pixel_auto"), checkMaxMpAuto->isChecked());
            const QString tiledMode = radioTiledAlways->isChecked() ? QStringLiteral("always") : QStringLiteral("automatic");
            s.insert(QStringLiteral("tiled_vae_mode"), tiledMode.isEmpty() ? QStringLiteral("automatic") : tiledMode);
            s.insert(QStringLiteral("tiled_vae_always"), tiledMode == QLatin1String("always"));
            s.insert(QStringLiteral("dynamic_caching"), checkDynCache->isChecked());
            s.insert(QStringLiteral("multi_threading"), checkMultiThread->isChecked());
            ComfyUIUtils::saveSettingsJson(s);
            refreshQueueResolutionRowVisibility();
            updateUpscaleTargetSize();
        };
        connect(comboPerfPreset, QOverload<int>::of(&QComboBox::currentIndexChanged), dlg, [savePerfSettings, updateCustomPerfVisible](int) {
            updateCustomPerfVisible();
            savePerfSettings();
        });
        connect(comboPerfPreset, QOverload<int>::of(&QComboBox::activated), this,
                [this, comboPerfPreset, sliderPerfBatch, labelPerfBatchVal, sliderPerfRes, labelPerfResVal, spinMaxMp,
                 checkMaxMpAuto, radioTiledAutomatic, radioTiledAlways, savePerfSettings, syncPerfSlidersFromDock](int) {
                    const QString key = comboPerfPreset->currentData().toString();
                    if (key == QLatin1String("custom")) {
                        savePerfSettings();
                        return;
                    }
                    if (key == QLatin1String("auto")) {
                        savePerfSettings();
                        syncPerformanceFromAutoPreset();
                        syncPerfSlidersFromDock();
                        return;
                    }
                    int batch = 4;
                    int maxMp = 8;
                    QString tiledMode = QStringLiteral("automatic");
                    if (key == QLatin1String("cpu")) {
                        batch = 1;
                        maxMp = 2;
                        tiledMode = QStringLiteral("automatic");
                    } else if (key == QLatin1String("low")) {
                        batch = 2;
                        maxMp = 2;
                        tiledMode = QStringLiteral("always");
                    } else if (key == QLatin1String("medium")) {
                        batch = 4;
                        maxMp = 6;
                        tiledMode = QStringLiteral("automatic");
                    } else if (key == QLatin1String("high")) {
                        batch = 6;
                        maxMp = 8;
                        tiledMode = QStringLiteral("automatic");
                    } else if (key == QLatin1String("cloud")) {
                        batch = 8;
                        maxMp = 6;
                        tiledMode = QStringLiteral("automatic");
                    } else {
                        savePerfSettings();
                        return;
                    }
                    if (m_d->spinBatchCount) m_d->spinBatchCount->setValue(batch);
                    m_d->resolutionMultiplier = 1.0;
                    if (m_d->sliderResolutionMultiplier) {
                        m_d->sliderResolutionMultiplier->blockSignals(true);
                        m_d->sliderResolutionMultiplier->setValue(10);
                        m_d->sliderResolutionMultiplier->blockSignals(false);
                    }
                    if (m_d->labelResolutionMultiplier) m_d->labelResolutionMultiplier->setText(QStringLiteral("1.0×"));
                    KSharedConfig::openConfig()->group("ComfyUIRemote").writeEntry("BatchCount", batch);
                    KSharedConfig::openConfig()->group("ComfyUIRemote").writeEntry("ResolutionMultiplier", 1.0);
                    sliderPerfBatch->blockSignals(true);
                    sliderPerfBatch->setValue(qBound(1, batch, 16));
                    sliderPerfBatch->blockSignals(false);
                    labelPerfBatchVal->setText(QString::number(sliderPerfBatch->value()));
                    sliderPerfRes->blockSignals(true);
                    sliderPerfRes->setValue(10);
                    sliderPerfRes->blockSignals(false);
                    labelPerfResVal->setText(QStringLiteral("1.0×"));
                    spinMaxMp->blockSignals(true);
                    spinMaxMp->setValue(maxMp);
                    spinMaxMp->blockSignals(false);
                    checkMaxMpAuto->blockSignals(true);
                    checkMaxMpAuto->setChecked(false);
                    checkMaxMpAuto->blockSignals(false);
                    spinMaxMp->setEnabled(true);
                    radioTiledAutomatic->blockSignals(true);
                    radioTiledAlways->blockSignals(true);
                    if (tiledMode == QLatin1String("always"))
                        radioTiledAlways->setChecked(true);
                    else
                        radioTiledAutomatic->setChecked(true);
                    radioTiledAutomatic->blockSignals(false);
                    radioTiledAlways->blockSignals(false);
                    savePerfSettings();
                });
        connect(spinActiveHistoryMb, QOverload<int>::of(&QSpinBox::valueChanged), this, [this, savePerfSettings](int) {
            savePerfSettings();
            pruneHistoryToStorageLimit();
            updateHistoryUsageLabel();
        });
        connect(spinStoredHistoryMb, QOverload<int>::of(&QSpinBox::valueChanged), dlg, savePerfSettings);
        connect(spinMaxMp, QOverload<int>::of(&QSpinBox::valueChanged), dlg, savePerfSettings);
        connect(checkMaxMpAuto, &QCheckBox::toggled, dlg, [spinMaxMp, savePerfSettings](bool on) {
            spinMaxMp->setEnabled(!on);
            savePerfSettings();
        });
        connect(bgTiledVae, &QButtonGroup::idClicked, dlg, [savePerfSettings](int) { savePerfSettings(); });
        connect(checkDynCache, &QCheckBox::toggled, dlg, savePerfSettings);
        connect(checkMultiThread, &QCheckBox::toggled, dlg, savePerfSettings);
        connect(comboDiffusionScaleMode, QOverload<int>::of(&QComboBox::currentIndexChanged), dlg, savePerfSettings);
        connect(spinUpscaleTileExtent, QOverload<int>::of(&QSpinBox::valueChanged), dlg, savePerfSettings);
        connect(sliderPerfBatch, &QSlider::valueChanged, this, [this, labelPerfBatchVal, savePerfSettings](int v) {
            labelPerfBatchVal->setText(QString::number(v));
            if (m_d->spinBatchCount) m_d->spinBatchCount->setValue(v);
            KSharedConfig::openConfig()->group("ComfyUIRemote").writeEntry("BatchCount", v);
            savePerfSettings();
        });
        connect(sliderPerfRes, &QSlider::valueChanged, this, [this, labelPerfResVal, savePerfSettings](int v) {
            const double mul = qMax(0.3, v / 10.0);
            labelPerfResVal->setText(QString::number(mul, 'f', 1) + QLatin1String("×"));
            m_d->resolutionMultiplier = mul;
            if (m_d->sliderResolutionMultiplier) {
                m_d->sliderResolutionMultiplier->blockSignals(true);
                m_d->sliderResolutionMultiplier->setValue(v);
                m_d->sliderResolutionMultiplier->blockSignals(false);
            }
            if (m_d->labelResolutionMultiplier)
                m_d->labelResolutionMultiplier->setText(labelPerfResVal->text());
            KSharedConfig::openConfig()->group("ComfyUIRemote").writeEntry("ResolutionMultiplier", mul);
            savePerfSettings();
        });
        perfLayout->addLayout(perfForm);
        perfLayout->addWidget(customPerfWidget);
        perfLayout->addStretch();
        stack->addWidget(perfPage);

        // Plugin tab (index 5) — §4.9 Plugin Information and Updates
        QWidget *pluginPage = new QWidget(dlg);
        QVBoxLayout *pluginLayout = new QVBoxLayout(pluginPage);
        QWidget *pluginHeaderWrap = new QWidget(pluginPage);
        QHBoxLayout *pluginHeaderLay = new QHBoxLayout(pluginHeaderWrap);
        QLabel *pluginLogo = new QLabel(pluginHeaderWrap);
        {
            const QPixmap pm = KisIconUtils::loadIcon(QStringLiteral("krita-branding")).pixmap(64, 64);
            if (!pm.isNull())
                pluginLogo->setPixmap(pm);
            else
                pluginLogo->setFixedSize(64, 64);
        }
        QLabel *pluginTitle = new QLabel(i18n("Generative AI for Krita"), pluginHeaderWrap);
        QFont pluginTitleFont = pluginTitle->font();
        pluginTitleFont.setPointSize(pluginTitleFont.pointSize() + 4);
        pluginTitleFont.setBold(true);
        pluginTitle->setFont(pluginTitleFont);
        pluginTitle->setWordWrap(true);
        pluginHeaderLay->addWidget(pluginLogo, 0, Qt::AlignTop);
        pluginHeaderLay->addWidget(pluginTitle, 1);
        pluginLayout->addWidget(pluginHeaderWrap);
        QLabel *versionLabel = new QLabel(i18n("Current version: %1", ComfyUIUtils::pluginVersion()), pluginPage);
        versionLabel->setWordWrap(true);
        pluginLayout->addWidget(versionLabel);
        QLabel *labelPluginLatest = new QLabel(pluginPage);
        labelPluginLatest->setWordWrap(true);
        m_d->pluginTabLatestVersionLabel = labelPluginLatest;
        pluginLayout->addWidget(labelPluginLatest);
        // §4.9 / §13.37: Check for updates on startup, Check for Updates, Download and Install
        QJsonObject settings = ComfyUIUtils::loadSettingsJson();
        bool autoUpdate = settings.value(QStringLiteral("auto_update")).toBool(true);
        QCheckBox *checkAutoUpdate = new QCheckBox(i18n("Check for updates on startup"), pluginPage);
        checkAutoUpdate->setChecked(autoUpdate);
        checkAutoUpdate->setToolTip(i18n("When enabled, the Welcome view will check for a new plugin version when shown."));
        connect(checkAutoUpdate, &QCheckBox::toggled, this, [this](bool checked) {
            QJsonObject s = ComfyUIUtils::loadSettingsJson();
            s.insert(QStringLiteral("auto_update"), checked);
            ComfyUIUtils::saveSettingsJson(s);
        });
        pluginLayout->addWidget(checkAutoUpdate);
        QPushButton *checkUpdateBtn = new QPushButton(i18n("Check for Updates"), pluginPage);
        connect(checkUpdateBtn, &QPushButton::clicked, this, &ComfyUIRemoteDock::slotCheckForUpdates);
        pluginLayout->addWidget(checkUpdateBtn);
        QPushButton *btnPluginDownload = new QPushButton(i18n("Download and Install"), pluginPage);
        btnPluginDownload->setEnabled(false);
        btnPluginDownload->setToolTip(i18n("Opens the download page when a newer plugin version is reported by the update check."));
        connect(btnPluginDownload, &QPushButton::clicked, this, [this]() {
            const QUrl u = !m_d->updateDownloadUrl.isEmpty() ? QUrl(m_d->updateDownloadUrl)
                                                              : QUrl(QStringLiteral("https://github.com/Acly/krita-ai-diffusion/releases"));
            if (u.isValid())
                QDesktopServices::openUrl(u);
        });
        m_d->pluginTabDownloadInstallButton = btnPluginDownload;
        pluginLayout->addWidget(btnPluginDownload);
        // §4.9: System Information — hint, Collect Diagnostics, View log files
        QLabel *sysInfoHeading = new QLabel(i18n("System Information"), pluginPage);
        sysInfoHeading->setStyleSheet(QStringLiteral("font-weight: bold;"));
        pluginLayout->addWidget(sysInfoHeading);
        QLabel *sysInfoHint = new QLabel(i18n("Please attach this information when reporting issues!"), pluginPage);
        sysInfoHint->setWordWrap(true);
        pluginLayout->addWidget(sysInfoHint);
        QPushButton *collectDiagBtn = new QPushButton(i18n("Collect Diagnostics"), pluginPage);
        connect(collectDiagBtn, &QPushButton::clicked, this, [this]() {
            QString diag = ComfyUIUtils::collectDiagnostics(ComfyUIUtils::pluginVersion(), true,
                                                            &m_d->objectInfoSpec58NodesPresent);
            if (QClipboard *cb = QApplication::clipboard()) {
                cb->setText(diag);
            }
            QDialog *diagDlg = new QDialog(this);
            diagDlg->setWindowTitle(i18n("Diagnostics"));
            QVBoxLayout *lay = new QVBoxLayout(diagDlg);
            QPlainTextEdit *te = new QPlainTextEdit(diagDlg);
            te->setReadOnly(true);
            te->setPlainText(diag);
            te->setMinimumSize(500, 300);
            lay->addWidget(te);
            QDialogButtonBox *box = new QDialogButtonBox(QDialogButtonBox::Ok, diagDlg);
            connect(box, &QDialogButtonBox::accepted, diagDlg, &QDialog::accept);
            lay->addWidget(box);
            diagDlg->exec();
            diagDlg->deleteLater();
        });
        pluginLayout->addWidget(collectDiagBtn);
        QPushButton *viewLogsBtn = new QPushButton(i18n("View log files"), pluginPage);
        connect(viewLogsBtn, &QPushButton::clicked, this, [this]() {
            QDesktopServices::openUrl(QUrl::fromLocalFile(ComfyUIUtils::pluginLogDir()));
        });
        pluginLayout->addWidget(viewLogsBtn);
        QLabel *supportHeading = new QLabel(i18n("Documentation and Support"), pluginPage);
        supportHeading->setStyleSheet(QStringLiteral("font-weight: bold;"));
        pluginLayout->addWidget(supportHeading);
        QPushButton *websiteButton = new QPushButton(i18n("Website"), pluginPage);
        connect(websiteButton, &QPushButton::clicked, this, []() {
            QDesktopServices::openUrl(QUrl(QStringLiteral("https://www.interstice.cloud")));
        });
        pluginLayout->addWidget(websiteButton);
        QPushButton *handbookButton = new QPushButton(i18n("Handbook: Guides and Tips"), pluginPage);
        connect(handbookButton, &QPushButton::clicked, this, []() {
            QDesktopServices::openUrl(QUrl(QStringLiteral("https://docs.interstice.cloud")));
        });
        pluginLayout->addWidget(handbookButton);
        QPushButton *githubButton = new QPushButton(i18n("GitHub"), pluginPage);
        connect(githubButton, &QPushButton::clicked, this, []() {
            QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/Acly/krita-ai-diffusion")));
        });
        pluginLayout->addWidget(githubButton);
        // §4.9 / §13.200: same heading — Issues, Discussions, Discord
        QPushButton *issuesButton = new QPushButton(i18n("Issues"), pluginPage);
        connect(issuesButton, &QPushButton::clicked, this, []() {
            QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/Acly/krita-ai-diffusion/issues")));
        });
        pluginLayout->addWidget(issuesButton);
        QPushButton *discussionsButton = new QPushButton(i18n("Discussions"), pluginPage);
        connect(discussionsButton, &QPushButton::clicked, this, []() {
            QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/Acly/krita-ai-diffusion/discussions")));
        });
        pluginLayout->addWidget(discussionsButton);
        QPushButton *discordButton = new QPushButton(i18n("Discord"), pluginPage);
        connect(discordButton, &QPushButton::clicked, this, []() {
            QDesktopServices::openUrl(QUrl(QStringLiteral("https://discord.gg/pWyzHfHHhU")));
        });
        pluginLayout->addWidget(discordButton);
        pluginLayout->addStretch();
        refreshPluginInformationTabUpdateUi();
        stack->addWidget(pluginPage);

        // Canonical refresh control for slotRefreshCheckpoints (hidden); Styles tab uses its own refresh button.
        if (m_d->btnRefreshCheckpoints) {
            m_d->btnRefreshCheckpoints->setParent(dlg);
            m_d->btnRefreshCheckpoints->hide();
        }

        // Footer (Restore Defaults, version text, open settings folder, Ok)
        QHBoxLayout *footerLayout = new QHBoxLayout();
        QPushButton *restoreButton = new QPushButton(i18n("Restore Defaults"), dlg);
        connect(restoreButton, &QPushButton::clicked, this, &ComfyUIRemoteDock::slotRestoreDefaults);
        footerLayout->addWidget(restoreButton);
        // §4.3 / §13.204: Plugin version from single source
        QLabel *footerVersion = new QLabel(i18n("Plugin version: %1", ComfyUIUtils::pluginVersion()), dlg);
        footerVersion->setAlignment(Qt::AlignCenter);
        footerVersion->setStyleSheet(QStringLiteral("color: gray; font-style: italic;"));
        footerLayout->addWidget(footerVersion, 1);
        QPushButton *openSettingsFolder = new QPushButton(i18n("Open Settings folder"), dlg);
        connect(openSettingsFolder, &QPushButton::clicked, this, [this]() {
            QDesktopServices::openUrl(QUrl::fromLocalFile(ComfyUIUtils::pluginUserDataDir()));
        });
        footerLayout->addWidget(openSettingsFolder);
        // §4.3: "Open Settings folder", then "Ok" on the right of the same footer row
        QPushButton *okButton = new QPushButton(i18n("Ok"), dlg);
        okButton->setDefault(true);
        okButton->setAutoDefault(true);
        connect(okButton, &QPushButton::clicked, dlg, [dlg]() {
            KSharedConfig::openConfig()->sync();
            dlg->accept();
        });
        footerLayout->addWidget(okButton);
        mainLayout->addLayout(footerLayout);

        connect(navList, &QListWidget::currentRowChanged, stack, &QStackedWidget::setCurrentIndex);
        connect(navList, &QListWidget::currentRowChanged, this, [this, syncStylesFromDock, syncPerfSlidersFromDock](int row) {
            if (row == 1)
                syncStylesFromDock();
            if (row == 4) {
                updateHistoryUsageLabel();
                syncPerformanceFromAutoPreset();
                syncPerfSlidersFromDock();
            }
        });
        updateHistoryUsageLabel();
        navList->setCurrentRow(0);

        refreshCustomWorkflowParameterPanel();
    }

    if (m_d->settingsDialog) {
        if (m_d->checkConfirmDiscardImage)
            m_d->checkConfirmDiscardImage->setChecked(KSharedConfig::openConfig()->group("ComfyUIRemote").readEntry("ConfirmDiscardImage", true));
        // §13.33: Restore Connection tab state from ServerMode (undefined → show initial setup; else show mode selector + panel)
        if (m_d->connectionStack) {
            QString serverMode = KSharedConfig::openConfig()->group("ComfyUIRemote").readEntry("ServerMode", QStringLiteral("undefined"));
            if (serverMode == QLatin1String("undefined")) {
                m_d->connectionStack->setCurrentIndex(0);
            } else {
                m_d->connectionStack->setCurrentIndex(1);
                if (m_d->innerConnectionStack) {
                    int idx = (serverMode == QLatin1String("cloud")) ? 0 : (serverMode == QLatin1String("managed")) ? 1 : 2;
                    m_d->innerConnectionStack->setCurrentIndex(idx);
                }
                if (m_d->connectionModeGroup) {
                    int id = (serverMode == QLatin1String("cloud")) ? 0 : (serverMode == QLatin1String("managed")) ? 1 : 2;
                    QAbstractButton *btn = m_d->connectionModeGroup->button(id);
                    if (btn) btn->setChecked(true);
                }
            }
        }
        refreshCustomWorkflowParameterPanel();
        refreshPluginInformationTabUpdateUi();
        m_d->settingsDialog->show();
        m_d->settingsDialog->raise();
        m_d->settingsDialog->activateWindow();
    }
}

void ComfyUIRemoteDock::slotRestoreDefaults()
{
    KSharedConfig::Ptr cfgPtr = KSharedConfig::openConfig();
    KConfigGroup cfg(cfgPtr, "ComfyUIRemote");
    cfg.deleteGroup();
    cfgPtr->sync();

    // Reapply default values to UI widgets where applicable (§3.1: persist to settings.json)
    if (m_d->editServerUrl) {
        m_d->editServerUrl->setText(QStringLiteral("127.0.0.1:8188"));
        QJsonObject settings = ComfyUIUtils::loadSettingsJson();
        settings.insert(QStringLiteral("server_url"), QStringLiteral("127.0.0.1:8188"));
        ComfyUIUtils::saveSettingsJson(settings);
    }
    if (m_d->comboCheckpoint) {
        m_d->comboCheckpoint->setCurrentIndex(0);
    }
    if (m_d->comboPreset) {
        rebuildPresetComboItems();
    }
    if (m_d->comboQuality) {
        m_d->comboQuality->setCurrentIndex(1);
    }
    if (m_d->comboSizePreset) {
        m_d->comboSizePreset->setCurrentIndex(0);
    }
    if (m_d->spinWidth) {
        m_d->spinWidth->setValue(512);
    }
    if (m_d->spinHeight) {
        m_d->spinHeight->setValue(512);
    }
    if (m_d->spinSteps) {
        m_d->spinSteps->setValue(20);
    }
    if (m_d->spinCfg) {
        m_d->spinCfg->setValue(8.0);
    }
    if (m_d->spinStrength) {
        m_d->spinStrength->setValue(100);
    }
    if (m_d->comboSampler) {
        m_d->comboSampler->setCurrentIndex(0);
    }
    if (m_d->comboWorkspace) {
        m_d->comboWorkspace->setCurrentIndex(0);
    }
    if (m_d->comboQueueMode) {
        m_d->comboQueueMode->setCurrentIndex(0);
    }
    if (m_d->spinBatchCount) {
        m_d->spinBatchCount->setValue(1);
    }
    if (m_d->sliderResolutionMultiplier) {
        m_d->resolutionMultiplier = 1.0;
        m_d->sliderResolutionMultiplier->setValue(10);
    }
    if (m_d->labelResolutionMultiplier) {
        m_d->labelResolutionMultiplier->setText(QStringLiteral("1.0×"));
    }

    if (m_d->checkFixedSeed) {
        m_d->checkFixedSeed->setChecked(false);
    }
    if (m_d->spinSeed) {
        m_d->spinSeed->setValue(0);
    }
    syncQueueSeedWidgetsFromMain();
    if (m_d->checkConfirmDiscardImage) {
        m_d->checkConfirmDiscardImage->setChecked(true);
    }
    // §5.4: Reset Region-only, Edit mode, Layer count
    if (m_d->checkRegionOnly) {
        m_d->checkRegionOnly->setChecked(false);
    }
    if (m_d->checkEditMode) {
        m_d->checkEditMode->setChecked(false);
    }
    if (m_d->spinLayerCount) {
        m_d->spinLayerCount->setValue(1);
    }
    // §5.6, §5.7: Full Animation / Single Frame — restore to Single Frame
    if (m_d->radioSingleFrame) m_d->radioSingleFrame->setChecked(true);
    if (m_d->radioFullAnimation) m_d->radioFullAnimation->setChecked(false);
    cfg.writeEntry("FullAnimation", false);
    cfg.writeEntry(QStringLiteral("InpaintUseModel"), true);
    cfg.writeEntry(QStringLiteral("InpaintUsePromptFocus"), false);
    m_d->inpaintPersistUseModel = true;
    m_d->inpaintPersistUsePromptFocus = false;
    if (m_d->checkInpaintUseModel)
        m_d->checkInpaintUseModel->setChecked(true);
    if (m_d->checkInpaintUsePromptFocus)
        m_d->checkInpaintUsePromptFocus->setChecked(false);
    updateAnimationButtonLabel();

    // Clear history and regions, then refresh lists and persist
    m_d->historyEntries.clear();
    m_d->pendingHistoryByPromptId.clear();
    refreshHistoryList();

    m_d->regionEntries.clear();
    m_d->editRegionEntries.clear();
    saveRegionsToConfig();
    refreshRegionsList();

    // §13.33: After Restore Defaults, ServerMode is cleared (read as "undefined"); show initial setup again
    if (m_d->connectionStack) {
        m_d->connectionStack->setCurrentIndex(0);
    }
}
