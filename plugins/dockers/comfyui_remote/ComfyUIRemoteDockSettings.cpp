/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyUIRemoteDock.h"
#include "ComfyLocalization.h"
#include "ComfyUIIntervalSlider.h"
#include "ComfyUIRemoteDockPrivate.h"
#include "ComfyUIUtils.h"
#include "ComfyStyleCollection.h"
#include "ComfyResources.h"
#include "ComfyTheme.h"
#include "ComfySwitchWidget.h"
#include "ComfyFileLibrary.h"
#include "ComfyStyleLoraListWidget.h"
#include "ComfyStyleSamplerWidget.h"

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

namespace {
// §13.140: NSFW filter warning — QMessageBox.warning once per session (not persisted in KConfig)
bool g_nsfwFilterWarningShownThisSession = false;
} // namespace

void ComfyUIRemoteDock::slotConfigureHelp()
{
    // FAITHFUL_PORT: rebuild the dock-side preset combo BEFORE the Settings
    // dialog is constructed. The Styles tab's stylesPresetMirror is populated
    // from m_d->comboPreset, so if the bundled ComfyStyleCollection hadn't been
    // reloaded yet (e.g. first time opening Settings on Android after a fresh
    // install) the mirror only saw the placeholder "None" entry and the
    // dropdown popup looked empty / non-functional.
    if (m_d->comboPreset)
        rebuildPresetComboItems();
    if (!m_d->settingsDialog) {
        QDialog *dlg = new QDialog(this);
        dlg->setWindowTitle(ComfyTr::tr("Configure Image Diffusion"));
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
        // FAITHFUL_PORT: Plugin tab dropped on Android — the auto-update path and
        // diagnostics buttons there expose host-only flows (URL handlers, intent
        // launchers) that crash or no-op on Android; the docker's About menu plus
        // the footer Ok button cover the remaining info.
        const QStringList navItems = { ComfyTr::tr("Connection"), ComfyTr::tr("Styles"), ComfyTr::tr("Diffusion"), ComfyTr::tr("Interface"), ComfyTr::tr("Performance") };
        for (const QString &label : navItems) {
            QListWidgetItem *item = new QListWidgetItem(label);
            item->setSizeHint(QSize(112, 24));  // §13.175
            navList->addItem(item);
        }
        contentLayout->addWidget(navList);

        QStackedWidget *stack = new QStackedWidget(dlg);
        contentLayout->addWidget(stack, 1);

        // Connection tab: this native build only supports connecting to a user-managed ComfyUI URL.
        m_d->connectionStack = new QStackedWidget(dlg);

        QWidget *connectionPage = new QWidget(dlg);
        QVBoxLayout *connectionLayout = new QVBoxLayout(connectionPage);
        QLabel *connTabHeading = new QLabel(ComfyTr::tr("Server Configuration"), connectionPage);
        QFont connTabHeadingFont = connTabHeading->font();
        connTabHeadingFont.setBold(true);
        connTabHeading->setFont(connTabHeadingFont);
        connectionLayout->addWidget(connTabHeading);

        QLabel *serverUrlDesc = new QLabel(ComfyTr::tr("URL used to connect to a running ComfyUI server. Default is 127.0.0.1:8188 (local)."), connectionPage);
        serverUrlDesc->setWordWrap(true);
        connectionLayout->addWidget(serverUrlDesc);
        QFormLayout *connForm = new QFormLayout();
        connForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
        connForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        connForm->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
        connForm->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        connForm->setHorizontalSpacing(12);
        connForm->setVerticalSpacing(6);
        connForm->addRow(ComfyTr::tr("Server URL:"), m_d->editServerUrl);
        connectionLayout->addLayout(connForm);

        m_d->btnTest = new QPushButton(ComfyTr::tr("Connect"), connectionPage);
        m_d->btnTest->setIcon(ComfyTheme::icon(QStringLiteral("web-connection")));
        connect(m_d->btnTest, &QPushButton::clicked, this, [this](bool) {
            if (m_d->isConnected)
                slotDisconnect();
            else {
                m_d->connectionAutostartActive = false;
                cancelConnectionAutostartRetry();
                slotTestConnection();
            }
        });
        connectionLayout->addWidget(m_d->btnTest);

        m_d->labelConnectionStatus = new QLabel(connectionPage);
        m_d->labelConnectionStatus->setStyleSheet(QStringLiteral("color: gray;"));
        connectionLayout->addWidget(m_d->labelConnectionStatus);

        // §4.4 / §7.4: Detected base models — list of architectures with supported/missing status (populated when connected)
        QLabel *detectedModelsHeading = new QLabel(ComfyTr::tr("Detected base models:"), connectionPage);
        connectionLayout->addWidget(detectedModelsHeading);
        m_d->labelDetectedModels = new QLabel(ComfyTr::tr("Connect to server to see detected architectures (SD 1.5, SD XL, Flux, etc.)."), connectionPage);
        m_d->labelDetectedModels->setWordWrap(true);
        m_d->labelDetectedModels->setStyleSheet(QStringLiteral("color: gray;"));
        connectionLayout->addWidget(m_d->labelDetectedModels);
        QLabel *connHelp = new QLabel(
            ComfyTr::tr("Install the required custom nodes and models on your ComfyUI server. Check the client.log file for more details."),
            connectionPage);
        connHelp->setWordWrap(true);
        connectionLayout->addWidget(connHelp);
        connectionLayout->addStretch();

        m_d->connectionStack->addWidget(connectionPage);
        stack->addWidget(m_d->connectionStack);

        KConfigGroup connectionCfg = KSharedConfig::openConfig()->group("ComfyUIRemote");
        const QString initialMode = connectionCfg.readEntry("ServerMode", QStringLiteral("external"));
        if (initialMode != QLatin1String("external")) {
            connectionCfg.writeEntry("ServerMode", QStringLiteral("external"));
            KSharedConfig::openConfig()->sync();
        }

        // Styles tab (index 1) — §4.5 Style Presets (mirrors dock; LoRA list + samplers.json presets partial / stub)
        QWidget *stylesPage = new QWidget(dlg);
        QVBoxLayout *stylesOuter = new QVBoxLayout(stylesPage);
        QScrollArea *stylesScroll = new QScrollArea(stylesPage);
        stylesScroll->setWidgetResizable(true);
        stylesScroll->setFrameShape(QFrame::NoFrame);
        QWidget *stylesInner = new QWidget();
        QVBoxLayout *stylesLayout = new QVBoxLayout(stylesInner);
        QLabel *stylesHeading = new QLabel(ComfyTr::tr("Style Presets"), stylesInner);
        QFont stylesHeadingFont = stylesHeading->font();
        stylesHeadingFont.setBold(true);
        stylesHeading->setFont(stylesHeadingFont);
        stylesLayout->addWidget(stylesHeading);

        QFrame *styleToolbarFrame = new QFrame(stylesInner);
        styleToolbarFrame->setFrameStyle(QFrame::StyledPanel);
        styleToolbarFrame->setLineWidth(1);
        QVBoxLayout *styleToolbarLayout = new QVBoxLayout(styleToolbarFrame);
        QHBoxLayout *presetBtnRow = new QHBoxLayout();
        QComboBox *stylesPresetMirror = new QComboBox(styleToolbarFrame);
        QToolButton *btnStylesAddPreset = new QToolButton(styleToolbarFrame);
        btnStylesAddPreset->setIcon(ComfyTheme::icon(QStringLiteral("control-add")));
        btnStylesAddPreset->setToolTip(ComfyTr::tr("Create a new style"));
        btnStylesAddPreset->setAutoRaise(true);
        QToolButton *btnStylesDuplicate = new QToolButton(styleToolbarFrame);
        btnStylesDuplicate->setIcon(ComfyTheme::icon(QStringLiteral("edit")));
        btnStylesDuplicate->setToolTip(ComfyTr::tr("Duplicate the current style"));
        btnStylesDuplicate->setAutoRaise(true);
        QToolButton *btnStylesDeletePreset = new QToolButton(styleToolbarFrame);
        btnStylesDeletePreset->setIcon(ComfyTheme::icon(QStringLiteral("discard")));
        btnStylesDeletePreset->setToolTip(ComfyTr::tr("Delete the current style"));
        btnStylesDeletePreset->setAutoRaise(true);
        QToolButton *btnStylesRefresh = new QToolButton(styleToolbarFrame);
        btnStylesRefresh->setIcon(ComfyTheme::icon(QStringLiteral("reset")));
        btnStylesRefresh->setToolTip(ComfyTr::tr("Look for new style files"));
        btnStylesRefresh->setAutoRaise(true);
        connect(btnStylesDeletePreset, &QToolButton::clicked, this, &ComfyUIRemoteDock::slotDeletePreset);
        presetBtnRow->addWidget(stylesPresetMirror, 1);
        presetBtnRow->addWidget(btnStylesAddPreset);
        presetBtnRow->addWidget(btnStylesDuplicate);
        presetBtnRow->addWidget(btnStylesDeletePreset);
        presetBtnRow->addWidget(btnStylesRefresh);
        styleToolbarLayout->addLayout(presetBtnRow);

        QLabel *lblBuiltinMessage = new QLabel(ComfyTr::tr("Built-in styles cannot be modified."), styleToolbarFrame);
        lblBuiltinMessage->setStyleSheet(QStringLiteral("font-style: italic;"));
        lblBuiltinMessage->hide();
        QLabel *lblBuiltinCopyLink = new QLabel(
            QStringLiteral("<a href=\"copy\">%1</a>").arg(ComfyTr::tr("Click to edit a copy")), styleToolbarFrame);
        lblBuiltinCopyLink->setTextFormat(Qt::RichText);
        lblBuiltinCopyLink->setTextInteractionFlags(Qt::TextBrowserInteraction);
        lblBuiltinCopyLink->setOpenExternalLinks(false);
        lblBuiltinCopyLink->hide();
        QHBoxLayout *builtinLayout = new QHBoxLayout();
        builtinLayout->setContentsMargins(6, 1, 1, 1);
        builtinLayout->addWidget(lblBuiltinMessage);
        builtinLayout->addWidget(lblBuiltinCopyLink);
        builtinLayout->addStretch();
        QCheckBox *checkShowBuiltinStyles = new QCheckBox(ComfyTr::tr("Show pre-installed styles"), styleToolbarFrame);
        checkShowBuiltinStyles->setChecked(
            ComfyUIUtils::loadSettingsJson().value(QStringLiteral("show_builtin_styles")).toBool(true));
        builtinLayout->addWidget(checkShowBuiltinStyles);
        styleToolbarLayout->addLayout(builtinLayout);
        stylesLayout->addWidget(styleToolbarFrame);

        auto addStylesBoldHeader = [stylesInner](const QString &title) -> QLabel * {
            QLabel *titleLabel = new QLabel(title, stylesInner);
            QFont titleFont = titleLabel->font();
            titleFont.setBold(true);
            titleLabel->setFont(titleFont);
            return titleLabel;
        };
        auto addStylesSettingRow = [stylesInner](const QString &title, const QString &description, QWidget *control) -> QWidget * {
            QWidget *row = new QWidget(stylesInner);
            QHBoxLayout *rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(0, 4, 0, 4);
            QVBoxLayout *textCol = new QVBoxLayout();
            textCol->setContentsMargins(0, 0, 0, 0);
            textCol->setSpacing(2);
            QLabel *titleLabel = new QLabel(title, row);
            QFont titleFont = titleLabel->font();
            titleFont.setBold(true);
            titleLabel->setFont(titleFont);
            textCol->addWidget(titleLabel);
            if (!description.isEmpty()) {
                QLabel *descLabel = new QLabel(description, row);
                descLabel->setWordWrap(true);
                textCol->addWidget(descLabel);
            }
            rowLayout->addLayout(textCol, 5);
            if (control)
                rowLayout->addWidget(control, 0, Qt::AlignRight | Qt::AlignVCenter);
            else
                rowLayout->addStretch(1);
            return row;
        };
        // Python LineEditSetting: header block then full-width line edit below.
        auto addStylesLineEditBlock = [stylesInner, addStylesBoldHeader](const QString &title, const QString &description,
                                                                         QLineEdit *edit) -> QWidget * {
            QWidget *block = new QWidget(stylesInner);
            QVBoxLayout *blockLayout = new QVBoxLayout(block);
            blockLayout->setContentsMargins(0, 4, 0, 4);
            blockLayout->setSpacing(4);
            blockLayout->addWidget(addStylesBoldHeader(title));
            if (!description.isEmpty()) {
                QLabel *descLabel = new QLabel(description, block);
                descLabel->setWordWrap(true);
                blockLayout->addWidget(descLabel);
            }
            edit->setMinimumWidth(0);
            edit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            blockLayout->addWidget(edit);
            return block;
        };

        QLineEdit *editStyleName = new QLineEdit(stylesInner);
        stylesLayout->addWidget(addStylesSettingRow(ComfyTr::tr("Name"), QString(), editStyleName));

        QHBoxLayout *ckptRow = new QHBoxLayout();
        QComboBox *stylesCkptMirror = new QComboBox(stylesInner);
        stylesCkptMirror->setEditable(true);
        stylesCkptMirror->setMinimumWidth(230);
        stylesCkptMirror->setPlaceholderText(ComfyTr::tr("The Diffusion model checkpoint file"));
        QToolButton *btnStylesCkptRefresh = new QToolButton(stylesInner);
        btnStylesCkptRefresh->setIcon(ComfyTheme::icon(QStringLiteral("reset")));
        btnStylesCkptRefresh->setToolTip(ComfyTr::tr("Look for new checkpoint files"));
        btnStylesCkptRefresh->setAutoRaise(true);
        connect(btnStylesCkptRefresh, &QToolButton::clicked, this, &ComfyUIRemoteDock::slotRefreshCheckpoints);
        ckptRow->addWidget(stylesCkptMirror, 1);
        ckptRow->addWidget(btnStylesCkptRefresh);
        QWidget *ckptControl = new QWidget(stylesInner);
        ckptControl->setLayout(ckptRow);
        stylesLayout->addWidget(addStylesSettingRow(
            ComfyTr::tr("Model Checkpoint"),
            ComfyTr::tr("The Diffusion model checkpoint file"),
            ckptControl));
        QLabel *stylesCkptWarning = new QLabel(stylesInner);
        stylesCkptWarning->setWordWrap(true);
        stylesCkptWarning->setStyleSheet(QStringLiteral("color: #b8860b; font-style: italic;"));
        stylesCkptWarning->setAlignment(Qt::AlignRight);
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
        toggleAdvCkpt->setText(ComfyTr::tr("Checkpoint configuration (advanced)"));
        QWidget *advCkptBody = new QWidget(stylesInner);
        QVBoxLayout *advCkptLay = new QVBoxLayout(advCkptBody);
        advCkptLay->setContentsMargins(0, 0, 0, 0);
        advCkptLay->setSpacing(0);
        auto addAdvCkptSettingRow = [advCkptBody, advCkptLay](const QString &title, const QString &description) -> QHBoxLayout * {
            QWidget *row = new QWidget(advCkptBody);
            QHBoxLayout *rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(16, 4, 0, 4);
            QVBoxLayout *textCol = new QVBoxLayout();
            textCol->setContentsMargins(0, 0, 0, 0);
            textCol->setSpacing(2);
            QLabel *titleLabel = new QLabel(title, row);
            QFont titleFont = titleLabel->font();
            titleFont.setBold(true);
            titleLabel->setFont(titleFont);
            textCol->addWidget(titleLabel);
            if (!description.isEmpty()) {
                QLabel *descLabel = new QLabel(description, row);
                descLabel->setWordWrap(true);
                textCol->addWidget(descLabel);
            }
            rowLayout->addLayout(textCol, 5);
            rowLayout->addStretch(1);
            advCkptLay->addWidget(row);
            return rowLayout;
        };
        QComboBox *comboStyleArchitecture = new QComboBox(advCkptBody);
        comboStyleArchitecture->setMinimumWidth(230);
        comboStyleArchitecture->setToolTip(
            ComfyTr::tr("Architecture for the checkpoint. Automatic resolves from the model at generate time."));
        QComboBox *comboStyleVae = new QComboBox(advCkptBody);
        comboStyleVae->setMinimumWidth(230);
        comboStyleVae->setToolTip(ComfyTr::tr("VAE used for encode/decode. Connect to the server and refresh to list installed VAE files."));
        QSpinBox *spinStyleClipSkip = new QSpinBox(advCkptBody);
        spinStyleClipSkip->setRange(0, 12);
        spinStyleClipSkip->setMinimumWidth(100);
        spinStyleClipSkip->setSpecialValueText(ComfyTr::tr("Default"));
        spinStyleClipSkip->setToolTip(ComfyTr::tr("CLIP skip layers (SD 1.5 / SDXL / Illustrious). 0 uses the checkpoint default."));
        QCheckBox *checkStyleClipSkipOverride = new QCheckBox(ComfyTr::tr("Override"), advCkptBody);
        QSpinBox *spinStylePreferredResolution = new QSpinBox(advCkptBody);
        spinStylePreferredResolution->setRange(0, 2048);
        spinStylePreferredResolution->setSingleStep(8);
        spinStylePreferredResolution->setMinimumWidth(100);
        spinStylePreferredResolution->setSpecialValueText(ComfyTr::tr("Default"));
        spinStylePreferredResolution->setToolTip(ComfyTr::tr("When enabled, sets generate width/height to this square size when the style is applied."));
        QCheckBox *checkStylePreferredResolution = new QCheckBox(ComfyTr::tr("Override"), advCkptBody);
        ComfySwitchWidget *switchStyleZsnr = new ComfySwitchWidget(advCkptBody);
        switchStyleZsnr->setToolTip(ComfyTr::tr("v-prediction zsnr (saved to style JSON; workflow nodes deferred)."));
        QLabel *labelStyleZsnrState = new QLabel(ComfyTr::tr("Off"), advCkptBody);
        ComfySwitchWidget *switchStyleSag = new ComfySwitchWidget(advCkptBody);
        switchStyleSag->setToolTip(ComfyTr::tr("Self-attention guidance (saved to style JSON; workflow nodes deferred)."));
        QLabel *labelStyleSagState = new QLabel(ComfyTr::tr("Off"), advCkptBody);
        {
            QHBoxLayout *archRow = addAdvCkptSettingRow(
                ComfyTr::tr("Diffusion Architecture"),
                ComfyTr::tr("The base model ecosystem which the selected checkpoint belongs to."));
            archRow->addWidget(comboStyleArchitecture);
        }
        {
            QHBoxLayout *vaeRow = addAdvCkptSettingRow(
                ComfyTr::tr("VAE"),
                ComfyTr::tr("Model to encode and decode images. Commonly affects saturation and sharpness."));
            vaeRow->addWidget(comboStyleVae);
        }
        {
            QHBoxLayout *clipRow = addAdvCkptSettingRow(
                ComfyTr::tr("Clip Skip"),
                ComfyTr::tr("Clip layers to omit at the end. Some checkpoints prefer a different value than the default."));
            clipRow->addWidget(checkStyleClipSkipOverride);
            clipRow->addWidget(spinStyleClipSkip);
        }
        {
            QHBoxLayout *resRow = addAdvCkptSettingRow(
                ComfyTr::tr("Preferred Resolution"),
                ComfyTr::tr("Image resolution the checkpoint was trained on"));
            resRow->addWidget(checkStylePreferredResolution);
            resRow->addWidget(spinStylePreferredResolution);
        }
        {
            QHBoxLayout *zsnrRow = addAdvCkptSettingRow(
                ComfyTr::tr("V-Prediction / Zero Terminal SNR"),
                ComfyTr::tr("Enable this if the checkpoint is a v-prediction model which requires zero terminal SNR noise schedule"));
            zsnrRow->addWidget(labelStyleZsnrState);
            zsnrRow->addWidget(switchStyleZsnr);
        }
        {
            QHBoxLayout *sagRow = addAdvCkptSettingRow(
                ComfyTr::tr("Enable SAG / Self-Attention Guidance"),
                ComfyTr::tr("Pay more attention to difficult parts of the image. Can improve fine details."));
            sagRow->addWidget(labelStyleSagState);
            sagRow->addWidget(switchStyleSag);
        }
        connect(switchStyleZsnr, &QAbstractButton::toggled, labelStyleZsnrState, [labelStyleZsnrState](bool on) {
            labelStyleZsnrState->setText(on ? ComfyTr::tr("On") : ComfyTr::tr("Off"));
        });
        connect(switchStyleSag, &QAbstractButton::toggled, labelStyleSagState, [labelStyleSagState](bool on) {
            labelStyleSagState->setText(on ? ComfyTr::tr("On") : ComfyTr::tr("Off"));
        });
        wireDisclosure(toggleAdvCkpt, advCkptBody, false);
        stylesLayout->addWidget(toggleAdvCkpt);
        stylesLayout->addWidget(advCkptBody);

        ComfyStyleLoraListWidget *loraListWidget = new ComfyStyleLoraListWidget(stylesInner);
        m_d->stylesTabLoraListWidget = loraListWidget;
        stylesLayout->addWidget(loraListWidget);

        QLineEdit *editStylesPositive = new QLineEdit(stylesInner);
        stylesLayout->addWidget(addStylesLineEditBlock(
            ComfyTr::tr("Style Prompt"),
            ComfyTr::tr("Text which is appended to all prompts. The {prompt} placeholder can be used to wrap prompts."),
            editStylesPositive));

        QLineEdit *editStylesNegative = new QLineEdit(stylesInner);
        stylesLayout->addWidget(addStylesLineEditBlock(
            ComfyTr::tr("Negative Prompt"),
            ComfyTr::tr("Textual description of things to avoid in generated images."),
            editStylesNegative));

        QComboBox *comboLinkedEditStyle = new QComboBox(stylesInner);
        comboLinkedEditStyle->setMinimumWidth(230);
        QWidget *linkedEditStyleRow = addStylesSettingRow(
            ComfyTr::tr("Linked Edit Style"),
            ComfyTr::tr("Select an alternative style for instruction-based editing"),
            comboLinkedEditStyle);
        stylesLayout->addWidget(linkedEditStyleRow);

        QLabel *samplerSectionDesc = new QLabel(
            ComfyTr::tr("Configure sampler type, steps and CFG to tweak the quality of generated images."), stylesInner);
        samplerSectionDesc->setWordWrap(true);
        stylesLayout->addWidget(addStylesBoldHeader(ComfyTr::tr("Sampler Settings")));
        stylesLayout->addWidget(samplerSectionDesc);

        ComfyStyleSamplerWidget *qualitySamplerWidget = new ComfyStyleSamplerWidget(ComfyStyleSamplerWidget::Kind::Quality, stylesInner);
        ComfyStyleSamplerWidget *liveSamplerWidget = new ComfyStyleSamplerWidget(ComfyStyleSamplerWidget::Kind::Live, stylesInner);
        stylesLayout->addWidget(qualitySamplerWidget);
        stylesLayout->addWidget(liveSamplerWidget);
        stylesLayout->addStretch();
        stylesScroll->setWidget(stylesInner);
        stylesOuter->addWidget(stylesScroll);
        stack->addWidget(stylesPage);

        m_d->stylesTabPersistingLoras = false;
        auto persistStyleLoras = [this, loraListWidget]() {
            if (m_d->stylesTabPersistingLoras || !m_d->comboPreset)
                return;
            const ComfyStyleEntry *st = currentJsonStyleEntry();
            if (!st || st->isBuiltin)
                return;
            ComfyStyleEntry e = *st;
            e.loras = loraListWidget->value();
            m_d->stylesTabPersistingLoras = true;
            saveStyleEntry(e);
            m_d->stylesTabPersistingLoras = false;
        };
        auto reloadStyleLorasFromPreset = [this, loraListWidget]() {
            if (!loraListWidget)
                return;
            m_d->stylesTabPersistingLoras = true;
            const QString styleId = encodeStyleIdFromPresetCombo(m_d->comboPreset);
            if (const ComfyStyleEntry *st = ComfyStyleCollection::instance().findByStyleId(styleId))
                loraListWidget->setValue(st->loras);
            else
                loraListWidget->setValue(QJsonArray());
            loraListWidget->setServerLoraFilenames(m_d->comfyServerLoraFilenames);
            loraListWidget->refreshFilters();
            m_d->stylesTabPersistingLoras = false;
        };
        reloadStyleLorasFromPreset();
        connect(loraListWidget, &ComfyStyleLoraListWidget::valueChanged, dlg, persistStyleLoras);
        connect(loraListWidget, &ComfyStyleLoraListWidget::refreshRequested, this, &ComfyUIRemoteDock::slotRefreshCheckpoints);

        // FAITHFUL_PORT/CRASH FIX: these were stack-locals, captured by reference by
        // syncStylesFromDock() / editStyles{Positive,Negative}::textChanged /
        // editStyleName::editingFinished. The Settings dialog is non-modal and
        // outlives slotConfigureHelp(), so once this function returned the
        // captured references dangled and clicking the Styles nav tab on
        // Android crashed in QListWidget::_q_emitCurrentItemChanged (stack
        // canary check failed on epilogue). Bind to d-pointer members instead.
        m_d->stylesTabPresetNameBaselineMember.clear();
        m_d->stylesTabSyncing = false;
        QString &stylesTabPresetNameBaseline = m_d->stylesTabPresetNameBaselineMember;
        bool &syncingStylesTab = m_d->stylesTabSyncing;
        auto repopulateLinkedEditStyleCombo = [this, comboLinkedEditStyle, linkedEditStyleRow, stylesCkptMirror]() {
            const QString currentStyleId = encodeStyleIdFromPresetCombo(m_d->comboPreset);
            QString ckpt = stylesCkptMirror->currentText().trimmed();
            QString styleArch = QStringLiteral("auto");
            if (const ComfyStyleEntry *cur = ComfyStyleCollection::instance().findByStyleId(currentStyleId)) {
                styleArch = cur->architecture;
                if (ckpt.isEmpty() && !cur->checkpoints.isEmpty())
                    ckpt = cur->checkpoints.first();
            }
            const ComfyResources::Arch curArch = ComfyWorkflowEngine::resolveArch(ckpt, styleArch);
            const bool hideLinked = ComfyResources::supportsEditInstructions(curArch);
            linkedEditStyleRow->setVisible(!hideLinked);
            if (hideLinked)
                return;
            QString saved;
            if (const ComfyStyleEntry *cur = ComfyStyleCollection::instance().findByStyleId(currentStyleId))
                saved = cur->linkedEditStyle.trimmed();
            comboLinkedEditStyle->blockSignals(true);
            comboLinkedEditStyle->clear();
            comboLinkedEditStyle->addItem(ComfyTr::tr("None"), QString());
            const bool showBuiltin =
                ComfyUIUtils::loadSettingsJson().value(QStringLiteral("show_builtin_styles")).toBool(true);
            for (const ComfyStyleEntry *s : ComfyStyleCollection::instance().filtered(showBuiltin)) {
                if (s->styleId == currentStyleId)
                    continue;
                const QString ckpt = s->checkpoints.isEmpty() ? QString() : s->checkpoints.first();
                const ComfyResources::Arch a = ComfyWorkflowEngine::resolveArch(ckpt, s->architecture);
                if (!ComfyResources::supportsEditInstructions(a))
                    continue;
                comboLinkedEditStyle->addItem(ComfyStyleCollection::comboDisplayName(*s), s->styleId);
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
        auto updateBuiltinStyleUi = [this, lblBuiltinMessage, lblBuiltinCopyLink, editStyleName, stylesCkptMirror,
                                     loraListWidget, editStylesPositive, editStylesNegative, comboLinkedEditStyle,
                                     qualitySamplerWidget, liveSamplerWidget, toggleAdvCkpt, advCkptBody,
                                     comboStyleArchitecture, comboStyleVae, spinStyleClipSkip, checkStyleClipSkipOverride,
                                     spinStylePreferredResolution, checkStylePreferredResolution, switchStyleZsnr,
                                     switchStyleSag, btnStylesDeletePreset]() {
            const ComfyStyleEntry *st = currentJsonStyleEntry();
            const bool hasJson = st != nullptr;
            const bool builtin = hasJson && st->isBuiltin;
            const bool editable = hasJson && !builtin;
            lblBuiltinMessage->setVisible(builtin);
            lblBuiltinCopyLink->setVisible(builtin);
            editStyleName->setReadOnly(!editable);
            stylesCkptMirror->setEnabled(editable);
            loraListWidget->setEditingEnabled(editable);
            editStylesPositive->setReadOnly(!editable);
            editStylesNegative->setReadOnly(!editable);
            comboLinkedEditStyle->setEnabled(editable);
            qualitySamplerWidget->setEditingEnabled(editable);
            liveSamplerWidget->setEditingEnabled(editable);
            toggleAdvCkpt->setEnabled(editable);
            advCkptBody->setEnabled(editable);
            comboStyleArchitecture->setEnabled(editable);
            comboStyleVae->setEnabled(editable);
            checkStyleClipSkipOverride->setEnabled(editable);
            spinStyleClipSkip->setEnabled(editable && checkStyleClipSkipOverride->isChecked());
            checkStylePreferredResolution->setEnabled(editable);
            spinStylePreferredResolution->setEnabled(editable && checkStylePreferredResolution->isChecked());
            switchStyleZsnr->setEnabled(editable);
            switchStyleSag->setEnabled(editable);
            btnStylesDeletePreset->setEnabled(editable);
        };
        auto syncStyleNameField = [editStyleName, &stylesTabPresetNameBaseline, this]() {
            if (!m_d->comboPreset)
                return;
            editStyleName->blockSignals(true);
            if (const ComfyStyleEntry *st = currentJsonStyleEntry()) {
                editStyleName->setText(st->name);
                stylesTabPresetNameBaseline = st->name;
            } else {
                editStyleName->clear();
                stylesTabPresetNameBaseline.clear();
            }
            editStyleName->blockSignals(false);
        };
        // FAITHFUL_PORT/CRASH FIX: this guard flag was a stack-local captured by
        // reference into syncAdvCkptFromStyle() and persistStyleCheckpointOptions(),
        // both connected to long-lived signals on widgets owned by the persistent
        // Settings dialog. After slotConfigureHelp() returned, the reference
        // dangled — opening the Styles nav tab later invoked the lambdas which
        // then read/wrote freed stack memory, corrupting the link register and
        // crashing in <unknown> with `lr == pc` style SIGSEGV. Bind to a d-pointer
        // member so the storage outlives the dialog.
        m_d->stylesTabPersistingAdvanced = false;
        bool &persistingStyleAdvanced = m_d->stylesTabPersistingAdvanced;
        auto resolvedStyleArch = [this, stylesCkptMirror]() -> ComfyResources::Arch {
            QString ckpt = stylesCkptMirror->currentText().trimmed();
            QString styleArch = QStringLiteral("auto");
            const QString styleId = encodeStyleIdFromPresetCombo(m_d->comboPreset);
            if (const ComfyStyleEntry *st = ComfyStyleCollection::instance().findByStyleId(styleId)) {
                styleArch = st->architecture;
                if (ckpt.isEmpty() && !st->checkpoints.isEmpty())
                    ckpt = st->checkpoints.first();
            }
            if (ckpt.isEmpty() && m_d->comboCheckpoint)
                ckpt = m_d->comboCheckpoint->currentText().trimmed();
            return ComfyWorkflowEngine::resolveArch(ckpt, styleArch);
        };
        auto repopulateStyleArchitectureCombo = [this, comboStyleArchitecture, stylesCkptMirror](const QString &styleArchitectureKey) {
            QString ckpt = stylesCkptMirror->currentText().trimmed();
            const QString styleId = encodeStyleIdFromPresetCombo(m_d->comboPreset);
            if (ckpt.isEmpty()) {
                if (const ComfyStyleEntry *st = ComfyStyleCollection::instance().findByStyleId(styleId)) {
                    if (!st->checkpoints.isEmpty())
                        ckpt = st->checkpoints.first();
                }
            }
            if (ckpt.isEmpty() && m_d->comboCheckpoint)
                ckpt = m_d->comboCheckpoint->currentText().trimmed();
            const ComfyResources::Arch resolved = ComfyWorkflowEngine::resolveArch(ckpt, styleArchitectureKey);
            const QVector<QString> keys = ComfyResources::validArchitectureKeysForResolvedArch(resolved);
            const QString wantKey =
                styleArchitectureKey.trimmed().isEmpty() ? QStringLiteral("auto") : styleArchitectureKey.trimmed().toLower();
            comboStyleArchitecture->blockSignals(true);
            comboStyleArchitecture->clear();
            for (const QString &key : keys)
                comboStyleArchitecture->addItem(ComfyResources::architectureKeyDisplayName(key), key);
            int pick = comboStyleArchitecture->findData(wantKey);
            if (pick < 0)
                pick = comboStyleArchitecture->findData(QStringLiteral("auto"));
            if (pick >= 0)
                comboStyleArchitecture->setCurrentIndex(pick);
            comboStyleArchitecture->blockSignals(false);
        };
        auto repopulateStyleVaeCombo = [this, comboStyleVae]() {
            const QString prev = comboStyleVae->currentText();
            comboStyleVae->blockSignals(true);
            comboStyleVae->clear();
            comboStyleVae->addItem(QStringLiteral("Checkpoint Default"));
            for (const QString &v : ComfyUIUtils::vaeNamesFromObjectInfo(m_d->lastObjectInfoRoot))
                comboStyleVae->addItem(v);
            int fi = comboStyleVae->findText(prev);
            if (fi >= 0)
                comboStyleVae->setCurrentIndex(fi);
            else if (!prev.isEmpty()) {
                comboStyleVae->addItem(prev);
                comboStyleVae->setCurrentText(prev);
            }
            comboStyleVae->blockSignals(false);
        };
        auto updateStyleAdvancedArchUi = [resolvedStyleArch, spinStyleClipSkip, checkStyleClipSkipOverride, switchStyleZsnr,
                                          switchStyleSag]() {
            const ComfyResources::Arch arch = resolvedStyleArch();
            const bool clipOk = ComfyResources::supportsClipSkip(arch);
            checkStyleClipSkipOverride->setEnabled(clipOk);
            spinStyleClipSkip->setEnabled(clipOk && checkStyleClipSkipOverride->isChecked());
            const bool attnOk = ComfyResources::supportsAttentionGuidance(arch);
            switchStyleZsnr->setEnabled(attnOk);
            switchStyleSag->setEnabled(attnOk);
        };
        auto syncAdvCkptFromStyle = [this, comboStyleArchitecture, stylesCkptMirror, comboStyleVae, spinStyleClipSkip,
                                     checkStyleClipSkipOverride, spinStylePreferredResolution, checkStylePreferredResolution,
                                     switchStyleZsnr, switchStyleSag, labelStyleZsnrState, labelStyleSagState,
                                     &persistingStyleAdvanced, updateStyleAdvancedArchUi, repopulateStyleVaeCombo,
                                     repopulateStyleArchitectureCombo]() {
            repopulateStyleVaeCombo();
            const QString styleId = encodeStyleIdFromPresetCombo(m_d->comboPreset);
            const ComfyStyleEntry *st = ComfyStyleCollection::instance().findByStyleId(styleId);
            const bool hasStyle = st != nullptr;
            const QString styleArchKey = hasStyle ? st->architecture : QStringLiteral("auto");
            repopulateStyleArchitectureCombo(styleArchKey);
            persistingStyleAdvanced = true;
            if (hasStyle) {
                int vi = comboStyleVae->findText(st->vae);
                if (vi >= 0)
                    comboStyleVae->setCurrentIndex(vi);
                else if (!st->vae.isEmpty()) {
                    comboStyleVae->addItem(st->vae);
                    comboStyleVae->setCurrentText(st->vae);
                } else
                    comboStyleVae->setCurrentIndex(0);
                const bool clipOn = st->clipSkip > 0;
                checkStyleClipSkipOverride->setChecked(clipOn);
                spinStyleClipSkip->setValue(clipOn ? st->clipSkip : 0);
                const bool resOn = st->preferredResolution > 0;
                checkStylePreferredResolution->setChecked(resOn);
                spinStylePreferredResolution->setValue(resOn ? st->preferredResolution : 0);
                spinStylePreferredResolution->setEnabled(resOn);
                switchStyleZsnr->setChecked(st->vPredictionZsnr);
                labelStyleZsnrState->setText(st->vPredictionZsnr ? ComfyTr::tr("On") : ComfyTr::tr("Off"));
                switchStyleSag->setChecked(st->selfAttentionGuidance);
                labelStyleSagState->setText(st->selfAttentionGuidance ? ComfyTr::tr("On") : ComfyTr::tr("Off"));
            } else {
                comboStyleVae->setCurrentIndex(0);
                checkStyleClipSkipOverride->setChecked(false);
                spinStyleClipSkip->setValue(0);
                checkStylePreferredResolution->setChecked(false);
                spinStylePreferredResolution->setValue(0);
                spinStylePreferredResolution->setEnabled(false);
                switchStyleZsnr->setChecked(false);
                labelStyleZsnrState->setText(ComfyTr::tr("Off"));
                switchStyleSag->setChecked(false);
                labelStyleSagState->setText(ComfyTr::tr("Off"));
            }
            persistingStyleAdvanced = false;
            updateStyleAdvancedArchUi();
        };
        auto persistCurrentJsonStyle = [this, editStyleName, stylesCkptMirror, editStylesPositive, editStylesNegative,
                                        qualitySamplerWidget, liveSamplerWidget, loraListWidget,
                                        comboStyleArchitecture, comboStyleVae, spinStyleClipSkip, checkStyleClipSkipOverride,
                                        spinStylePreferredResolution, checkStylePreferredResolution, switchStyleZsnr,
                                        switchStyleSag, comboLinkedEditStyle]() -> bool {
            const ComfyStyleEntry *st = currentJsonStyleEntry();
            if (!st || st->isBuiltin)
                return false;
            ComfyStyleEntry e = *st;
            e.name = editStyleName->text().trimmed().isEmpty() ? e.name : editStyleName->text().trimmed();
            const QString ckpt = stylesCkptMirror->currentText().trimmed();
            if (!ckpt.isEmpty())
                e.checkpoints = QStringList{ckpt};
            e.stylePrompt = editStylesPositive->text();
            e.negativePrompt = editStylesNegative->text();
            e.loras = loraListWidget->value();
            e.linkedEditStyle = comboLinkedEditStyle->currentData().toString();
            const QString archKey = comboStyleArchitecture->currentData().toString().trimmed();
            if (!archKey.isEmpty())
                e.architecture = archKey;
            e.vae = comboStyleVae->currentText();
            e.clipSkip = checkStyleClipSkipOverride->isChecked() ? spinStyleClipSkip->value() : 0;
            e.preferredResolution =
                checkStylePreferredResolution->isChecked() ? spinStylePreferredResolution->value() : 0;
            e.vPredictionZsnr = switchStyleZsnr->isChecked();
            e.selfAttentionGuidance = switchStyleSag->isChecked();
            qualitySamplerWidget->writeToStyle(&e);
            liveSamplerWidget->writeToStyle(&e);
            return saveStyleEntry(e);
        };
        auto persistStyleCheckpointOptions = [persistCurrentJsonStyle, &persistingStyleAdvanced]() {
            if (persistingStyleAdvanced)
                return;
            persistCurrentJsonStyle();
        };
        auto readJsonStyleIntoTab = [this, stylesCkptMirror, editStylesPositive, editStylesNegative, qualitySamplerWidget,
                                     liveSamplerWidget, &syncingStylesTab]() {
            syncingStylesTab = true;
            if (const ComfyStyleEntry *st = currentJsonStyleEntry()) {
                stylesCkptMirror->blockSignals(true);
                if (!st->checkpoints.isEmpty()) {
                    const QString ck = st->checkpoints.first();
                    int fi = stylesCkptMirror->findText(ck);
                    if (fi >= 0)
                        stylesCkptMirror->setCurrentIndex(fi);
                    else
                        stylesCkptMirror->setEditText(ck);
                }
                stylesCkptMirror->blockSignals(false);
                QString stylePrompt = st->stylePrompt;
                QString negativePrompt = st->negativePrompt;
                // User styles created before defaults were wired may have empty prompts on disk.
                if (!st->isBuiltin && stylePrompt.trimmed().isEmpty() && negativePrompt.trimmed().isEmpty()
                    && st->name == ComfyTr::tr("New Style")) {
                    stylePrompt = comfyDefaultStylePrompt();
                    negativePrompt = comfyDefaultNegativeStylePrompt();
                }
                editStylesPositive->setText(stylePrompt);
                editStylesNegative->setText(negativePrompt);
                qualitySamplerWidget->readFromStyle(*st);
                liveSamplerWidget->readFromStyle(*st);
            }
            syncingStylesTab = false;
        };
        auto updateStylesCkptWarning = [stylesCkptWarning, stylesCkptMirror, this]() {
            const ComfyStyleEntry *st = currentJsonStyleEntry();
            if (!st) {
                stylesCkptWarning->hide();
                return;
            }
            ComfyStyleEntry probe = *st;
            const QString ck = stylesCkptMirror->currentText().trimmed();
            if (!ck.isEmpty())
                probe.checkpoints = QStringList{ck};
            QStringList serverCkpts;
            if (m_d->comboCheckpoint) {
                for (int i = 0; i < m_d->comboCheckpoint->count(); ++i)
                    serverCkpts.append(m_d->comboCheckpoint->itemText(i));
            }
            const QStringList warn =
                ComfyUIUtils::styleCheckpointWarnings(probe, serverCkpts, m_d->lastObjectInfoRoot);
            if (warn.isEmpty()) {
                stylesCkptWarning->hide();
            } else {
                stylesCkptWarning->setText(warn.join(QLatin1Char('\n')));
                stylesCkptWarning->show();
            }
        };
        auto syncStylesFromDock = [this, stylesPresetMirror, stylesCkptMirror, updateStylesCkptWarning,
                                    repopulateLinkedEditStyleCombo, syncStyleNameField, reloadStyleLorasFromPreset,
                                    syncAdvCkptFromStyle, readJsonStyleIntoTab, updateBuiltinStyleUi, &syncingStylesTab]() {
            if (!m_d->comboPreset)
                return;
            ComfyUIUtils::reloadSamplerPresetsCache();
            syncingStylesTab = true;
            stylesPresetMirror->blockSignals(true);
            stylesPresetMirror->clear();
            for (int i = 0; i < m_d->comboPreset->count(); ++i)
                stylesPresetMirror->addItem(m_d->comboPreset->itemText(i), i);
            const int cur = m_d->comboPreset->currentIndex();
            const int mirrorIdx = stylesPresetMirror->findData(cur);
            stylesPresetMirror->setCurrentIndex(mirrorIdx >= 0 ? mirrorIdx : 0);
            stylesPresetMirror->blockSignals(false);
            if (m_d->comboCheckpoint) {
                stylesCkptMirror->blockSignals(true);
                stylesCkptMirror->clear();
                for (int i = 0; i < m_d->comboCheckpoint->count(); ++i)
                    stylesCkptMirror->addItem(m_d->comboCheckpoint->itemText(i));
                stylesCkptMirror->setCurrentIndex(m_d->comboCheckpoint->currentIndex());
                if (!m_d->comboCheckpoint->currentText().isEmpty()
                    && stylesCkptMirror->findText(m_d->comboCheckpoint->currentText()) < 0)
                    stylesCkptMirror->setEditText(m_d->comboCheckpoint->currentText());
                stylesCkptMirror->blockSignals(false);
            }
            syncingStylesTab = false;
            readJsonStyleIntoTab();
            syncStyleNameField();
            reloadStyleLorasFromPreset();
            syncAdvCkptFromStyle();
            repopulateLinkedEditStyleCombo();
            updateStylesCkptWarning();
            updateBuiltinStyleUi();
        };
        connect(btnStylesAddPreset, &QToolButton::clicked, this, [this, stylesCkptMirror, syncStylesFromDock]() {
            createJsonStyle(stylesCkptMirror->currentText().trimmed());
            syncStylesFromDock();
        });
        connect(btnStylesDuplicate, &QToolButton::clicked, this, [this, syncStylesFromDock]() {
            duplicateJsonStyle();
            syncStylesFromDock();
        });
        connect(lblBuiltinCopyLink, &QLabel::linkActivated, this, [this, syncStylesFromDock](const QString &) {
            duplicateJsonStyle();
            syncStylesFromDock();
        });
        connect(btnStylesRefresh, &QToolButton::clicked, this, [this, syncStylesFromDock]() {
            ComfyStyleCollection::instance().reload();
            rebuildPresetComboItems();
            slotRefreshCheckpoints();
            syncStylesFromDock();
        });
        // Also pick up out-of-dialog mutations to the preset list (refresh, delete,
        // etc.) so the mirror never goes stale. modelReset fires from clear()+addItem
        // bursts inside rebuildPresetComboItems(); rowsInserted/Removed cover the
        // incremental add/delete paths used by slotSaveAsPreset / slotDeletePreset.
        if (m_d->comboPreset) {
            connect(m_d->comboPreset->model(), &QAbstractItemModel::rowsInserted, dlg,
                    [syncStylesFromDock](const QModelIndex &, int, int) { syncStylesFromDock(); });
            connect(m_d->comboPreset->model(), &QAbstractItemModel::rowsRemoved, dlg,
                    [syncStylesFromDock](const QModelIndex &, int, int) { syncStylesFromDock(); });
            connect(m_d->comboPreset->model(), &QAbstractItemModel::modelReset, dlg,
                    [syncStylesFromDock]() { syncStylesFromDock(); });
        }
        connect(checkStyleClipSkipOverride, &QCheckBox::toggled, advCkptBody,
                [spinStyleClipSkip, checkStyleClipSkipOverride, resolvedStyleArch, updateStyleAdvancedArchUi,
                 persistStyleCheckpointOptions](bool on) {
                    spinStyleClipSkip->setEnabled(on);
                    if (on && spinStyleClipSkip->value() == 0) {
                        const ComfyResources::Arch a = resolvedStyleArch();
                        spinStyleClipSkip->setValue(a == ComfyResources::Arch::Sd15 ? 1 : 2);
                    } else if (!on)
                        spinStyleClipSkip->setValue(0);
                    updateStyleAdvancedArchUi();
                    persistStyleCheckpointOptions();
                });
        connect(checkStylePreferredResolution, &QCheckBox::toggled, advCkptBody,
                [spinStylePreferredResolution, resolvedStyleArch, persistStyleCheckpointOptions](bool on) {
                    spinStylePreferredResolution->setEnabled(on);
                    if (on && spinStylePreferredResolution->value() == 0) {
                        const ComfyResources::Arch a = resolvedStyleArch();
                        spinStylePreferredResolution->setValue(a == ComfyResources::Arch::Sd15 ? 640 : 1024);
                    } else if (!on && spinStylePreferredResolution->value() > 0) {
                        spinStylePreferredResolution->setValue(0);
                    }
                    persistStyleCheckpointOptions();
                });
        connect(comboStyleArchitecture, QOverload<int>::of(&QComboBox::currentIndexChanged), dlg,
                [persistStyleCheckpointOptions, updateStyleAdvancedArchUi](int) {
                    persistStyleCheckpointOptions();
                    updateStyleAdvancedArchUi();
                });
        connect(comboStyleVae, QOverload<int>::of(&QComboBox::currentIndexChanged), dlg,
                [persistStyleCheckpointOptions]() { persistStyleCheckpointOptions(); });
        connect(spinStyleClipSkip, QOverload<int>::of(&QSpinBox::valueChanged), dlg,
                [persistStyleCheckpointOptions]() { persistStyleCheckpointOptions(); });
        connect(spinStylePreferredResolution, QOverload<int>::of(&QSpinBox::valueChanged), dlg,
                [persistStyleCheckpointOptions]() { persistStyleCheckpointOptions(); });
        connect(switchStyleZsnr, &QAbstractButton::toggled, dlg,
                [persistStyleCheckpointOptions]() { persistStyleCheckpointOptions(); });
        connect(switchStyleSag, &QAbstractButton::toggled, dlg,
                [persistStyleCheckpointOptions]() { persistStyleCheckpointOptions(); });
        connect(qualitySamplerWidget, &ComfyStyleSamplerWidget::valueChanged, dlg,
                [persistCurrentJsonStyle]() { persistCurrentJsonStyle(); });
        connect(liveSamplerWidget, &ComfyStyleSamplerWidget::valueChanged, dlg,
                [persistCurrentJsonStyle]() { persistCurrentJsonStyle(); });
        connect(stylesPresetMirror, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [this, stylesPresetMirror, syncStylesFromDock, &syncingStylesTab](int) {
                    if (syncingStylesTab)
                        return;
                    const int dataIdx = stylesPresetMirror->currentData().toInt();
                    if (m_d->comboPreset && dataIdx >= 0 && dataIdx < m_d->comboPreset->count())
                        m_d->comboPreset->setCurrentIndex(dataIdx);
                    syncStylesFromDock();
                });
        connect(stylesCkptMirror, &QComboBox::currentTextChanged, this,
                [this, updateStylesCkptWarning, updateStyleAdvancedArchUi, persistStyleCheckpointOptions,
                 repopulateStyleArchitectureCombo, stylesCkptMirror](const QString &t) {
                    if (m_d->comboCheckpoint) {
                        int fi = m_d->comboCheckpoint->findText(t);
                        if (fi >= 0)
                            m_d->comboCheckpoint->setCurrentIndex(fi);
                        else
                            m_d->comboCheckpoint->setCurrentText(t);
                    }
                    const QString styleId = encodeStyleIdFromPresetCombo(m_d->comboPreset);
                    QString styleArch = QStringLiteral("auto");
                    if (const ComfyStyleEntry *st = ComfyStyleCollection::instance().findByStyleId(styleId))
                        styleArch = st->architecture;
                    repopulateStyleArchitectureCombo(styleArch);
                    updateStylesCkptWarning();
                    updateStyleAdvancedArchUi();
                    persistStyleCheckpointOptions();
                });
        connect(editStylesPositive, &QLineEdit::textChanged, this, [this, editStylesPositive, &syncingStylesTab, persistCurrentJsonStyle]() {
            if (syncingStylesTab)
                return;
            if (m_d->editPrompt)
                m_d->editPrompt->setPlainText(editStylesPositive->text());
            persistCurrentJsonStyle();
        });
        connect(editStylesNegative, &QLineEdit::textChanged, this, [this, editStylesNegative, &syncingStylesTab, persistCurrentJsonStyle]() {
            if (syncingStylesTab)
                return;
            if (m_d->editNegative)
                m_d->editNegative->setPlainText(editStylesNegative->text());
            persistCurrentJsonStyle();
        });
        connect(checkShowBuiltinStyles, &QCheckBox::toggled, dlg, [this, syncStylesFromDock](bool on) {
            QJsonObject s = ComfyUIUtils::loadSettingsJson();
            s.insert(QStringLiteral("show_builtin_styles"), on);
            ComfyUIUtils::saveSettingsJson(s);
            rebuildPresetComboItems();
            syncStylesFromDock();
        });
        connect(comboLinkedEditStyle, QOverload<int>::of(&QComboBox::currentIndexChanged), dlg,
                [persistCurrentJsonStyle]() { persistCurrentJsonStyle(); });
        connect(editStyleName, &QLineEdit::editingFinished, this,
                [this, editStyleName, &stylesTabPresetNameBaseline, persistCurrentJsonStyle, syncStylesFromDock]() {
                    const ComfyStyleEntry *st = currentJsonStyleEntry();
                    if (!st || st->isBuiltin)
                        return;
                    const QString oldName = stylesTabPresetNameBaseline.trimmed();
                    const QString newName = editStyleName->text().trimmed();
                    if (newName.isEmpty() || oldName == newName)
                        return;
                    if (!ComfyStyleCollection::instance().renameStyle(st->styleId, newName)) {
                        editStyleName->blockSignals(true);
                        editStyleName->setText(oldName);
                        editStyleName->blockSignals(false);
                        return;
                    }
                    stylesTabPresetNameBaseline = newName;
                    rebuildPresetComboItems();
                    applyStyleIdToPresetCombo(m_d->comboPreset, st->styleId);
                    syncStylesFromDock();
                });

        // Diffusion tab (index 2) — Python DiffusionSettings (settings.py L669–697)
        QWidget *diffusionPage = new QWidget(dlg);
        QVBoxLayout *diffusionOuter = new QVBoxLayout(diffusionPage);
        diffusionOuter->setContentsMargins(0, 0, 0, 0);
        QScrollArea *diffusionScroll = new QScrollArea(diffusionPage);
        diffusionScroll->setWidgetResizable(true);
        diffusionScroll->setFrameShape(QFrame::NoFrame);
        diffusionScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        QWidget *diffusionInner = new QWidget();
        QVBoxLayout *diffusionLayout = new QVBoxLayout(diffusionInner);
        QLabel *diffHeading = new QLabel(ComfyTr::tr("Diffusion Settings"), diffusionInner);
        QFont diffFont = diffHeading->font();
        diffFont.setBold(true);
        diffFont.setPointSize(diffFont.pointSize() + 2);
        diffHeading->setFont(diffFont);
        diffusionLayout->addWidget(diffHeading);
        diffusionLayout->addSpacing(6);

        auto makeDiffusionLabelColumn = [](QWidget *parent, const QString &title, const QString &description) -> QWidget * {
            auto *col = new QWidget(parent);
            auto *colLayout = new QVBoxLayout(col);
            colLayout->setContentsMargins(0, 0, 0, 0);
            colLayout->setSpacing(2);
            auto *titleLabel = new QLabel(title, col);
            QFont titleFont = titleLabel->font();
            titleFont.setBold(true);
            titleLabel->setFont(titleFont);
            colLayout->addWidget(titleLabel);
            if (!description.isEmpty()) {
                auto *descLabel = new QLabel(description, col);
                descLabel->setWordWrap(true);
                colLayout->addWidget(descLabel);
            }
            col->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
            return col;
        };
        auto addDiffusionSliderRow = [makeDiffusionLabelColumn](QWidget *parent, const QString &title, const QString &description,
                                        QSlider **outSlider, QLabel **outValueLabel, int min, int max,
                                        const QString &valueFormat) -> QWidget * {
            auto *row = new QWidget(parent);
            auto *rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(0, 4, 0, 4);
            rowLayout->addWidget(makeDiffusionLabelColumn(row, title, description), 1);
            auto *sliderBox = new QWidget(row);
            auto *sliderLayout = new QHBoxLayout(sliderBox);
            sliderLayout->setContentsMargins(0, 0, 0, 0);
            auto *slider = new QSlider(Qt::Horizontal, sliderBox);
            slider->setMinimumWidth(200);
            slider->setMaximumWidth(300);
            slider->setRange(min, max);
            auto *valueLabel = new QLabel(sliderBox);
            const QFontMetrics fm(valueLabel->font());
            valueLabel->setMinimumWidth(fm.horizontalAdvance(QStringLiteral("555 px")));
            valueLabel->setText(valueFormat);
            sliderLayout->addWidget(slider);
            sliderLayout->addWidget(valueLabel);
            rowLayout->addWidget(sliderBox, 0, Qt::AlignRight | Qt::AlignVCenter);
            *outSlider = slider;
            *outValueLabel = valueLabel;
            return row;
        };
        auto addDiffusionSwitchRow = [makeDiffusionLabelColumn](QWidget *parent, const QString &title, const QString &description,
                                        ComfySwitchWidget **outSwitch, QLabel **outStateLabel) -> QWidget * {
            auto *row = new QWidget(parent);
            auto *rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(0, 4, 0, 4);
            rowLayout->addWidget(makeDiffusionLabelColumn(row, title, description), 1);
            auto *stateLabel = new QLabel(ComfyTr::tr("Off"), row);
            auto *sw = new ComfySwitchWidget(row);
            rowLayout->addWidget(stateLabel, 0, Qt::AlignRight | Qt::AlignVCenter);
            rowLayout->addWidget(sw, 0, Qt::AlignRight | Qt::AlignVCenter);
            *outSwitch = sw;
            *outStateLabel = stateLabel;
            return row;
        };
        auto addDiffusionComboRow = [makeDiffusionLabelColumn](QWidget *parent, const QString &title, const QString &description,
                                       QComboBox **outCombo) -> QWidget * {
            auto *row = new QWidget(parent);
            auto *rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(0, 4, 0, 4);
            rowLayout->addWidget(makeDiffusionLabelColumn(row, title, description), 1);
            auto *combo = new QComboBox(row);
            combo->setMinimumWidth(230);
            rowLayout->addWidget(combo, 0, Qt::AlignRight | Qt::AlignVCenter);
            *outCombo = combo;
            return row;
        };

        QJsonObject diffSettings = ComfyUIUtils::loadSettingsJson();
        int selFeather = qBound(0, diffSettings.value(QStringLiteral("selection_feather")).toInt(10), 25);
        int selBlend = qBound(0, diffSettings.value(QStringLiteral("selection_blend")).toInt(25), 100);
        int selPadding = qBound(0, diffSettings.value(QStringLiteral("selection_padding")).toInt(6), 25);
        const bool colorMatch = diffSettings.value(QStringLiteral("color_match")).toBool(true);
        const double nsfwVal = qBound(0.0, diffSettings.value(QStringLiteral("nsfw_filter")).toDouble(0.0), 1.0);
        if (nsfwVal > 0.0)
            g_nsfwFilterWarningShownThisSession = true;

        QSlider *sliderSelectionFeather = nullptr;
        QLabel *labelFeatherVal = nullptr;
        diffusionLayout->addWidget(addDiffusionSliderRow(
            diffusionInner,
            ComfyTr::tr("Selection Feather"),
            ComfyTr::tr("The border is expanded and blurred by a fraction of selection size"),
            &sliderSelectionFeather,
            &labelFeatherVal,
            0,
            25,
            QStringLiteral("%1 %").arg(selFeather)));
        sliderSelectionFeather->setValue(selFeather);

        QSlider *sliderSelectionBlend = nullptr;
        QLabel *labelBlendVal = nullptr;
        diffusionLayout->addWidget(addDiffusionSliderRow(
            diffusionInner,
            ComfyTr::tr("Selection Blend"),
            ComfyTr::tr("Transition area for alpha blending the result image"),
            &sliderSelectionBlend,
            &labelBlendVal,
            0,
            100,
            QString::number(selBlend) + ComfyTr::tr(" px")));
        sliderSelectionBlend->setValue(selBlend);

        QSlider *sliderSelectionPadding = nullptr;
        QLabel *labelPaddingVal = nullptr;
        diffusionLayout->addWidget(addDiffusionSliderRow(
            diffusionInner,
            ComfyTr::tr("Selection Padding"),
            ComfyTr::tr("Minimum additional padding around the selection area"),
            &sliderSelectionPadding,
            &labelPaddingVal,
            0,
            25,
            QStringLiteral("%1 %").arg(selPadding)));
        sliderSelectionPadding->setValue(selPadding);

        ComfySwitchWidget *switchColorMatch = nullptr;
        QLabel *labelColorMatchState = nullptr;
        diffusionLayout->addWidget(addDiffusionSwitchRow(
            diffusionInner,
            ComfyTr::tr("Color Match"),
            ComfyTr::tr("Match peripheral colors and brightness with existing content. Requires a selection."),
            &switchColorMatch,
            &labelColorMatchState));
        switchColorMatch->setChecked(colorMatch);
        labelColorMatchState->setText(colorMatch ? ComfyTr::tr("On") : ComfyTr::tr("Off"));

        QComboBox *comboNsfwFilter = nullptr;
        diffusionLayout->addWidget(addDiffusionComboRow(
            diffusionInner,
            ComfyTr::tr("NSFW Filter"),
            ComfyTr::tr("Attempt to filter out images with explicit content"),
            &comboNsfwFilter));
        comboNsfwFilter->addItem(ComfyTr::tr("Disabled"), 0.0);
        comboNsfwFilter->addItem(ComfyTr::tr("Basic"), 0.65);
        comboNsfwFilter->addItem(ComfyTr::tr("Strict"), 0.8);
        const int nsfwIdx = (nsfwVal <= 0.0) ? 0 : (nsfwVal < 0.7) ? 1 : 2;
        comboNsfwFilter->setCurrentIndex(nsfwIdx);

        auto saveDiffusionSettings = [sliderSelectionFeather, sliderSelectionBlend, sliderSelectionPadding,
                                      switchColorMatch, comboNsfwFilter]() {
            QJsonObject s = ComfyUIUtils::loadSettingsJson();
            s.insert(QStringLiteral("selection_feather"), sliderSelectionFeather->value());
            s.insert(QStringLiteral("selection_blend"), sliderSelectionBlend->value());
            s.insert(QStringLiteral("selection_padding"), sliderSelectionPadding->value());
            s.insert(QStringLiteral("color_match"), switchColorMatch->isChecked());
            s.insert(QStringLiteral("nsfw_filter"), comboNsfwFilter->currentData().toDouble());
            ComfyUIUtils::saveSettingsJson(s);
        };
        connect(sliderSelectionFeather, &QSlider::valueChanged, dlg, [labelFeatherVal, saveDiffusionSettings](int v) {
            labelFeatherVal->setText(QStringLiteral("%1 %").arg(v));
            saveDiffusionSettings();
        });
        connect(sliderSelectionBlend, &QSlider::valueChanged, dlg, [labelBlendVal, saveDiffusionSettings](int v) {
            labelBlendVal->setText(QString::number(v) + ComfyTr::tr(" px"));
            saveDiffusionSettings();
        });
        connect(sliderSelectionPadding, &QSlider::valueChanged, dlg, [labelPaddingVal, saveDiffusionSettings](int v) {
            labelPaddingVal->setText(QStringLiteral("%1 %").arg(v));
            saveDiffusionSettings();
        });
        connect(switchColorMatch, &QAbstractButton::toggled, dlg, [labelColorMatchState, saveDiffusionSettings](bool on) {
            labelColorMatchState->setText(on ? ComfyTr::tr("On") : ComfyTr::tr("Off"));
            saveDiffusionSettings();
        });
        connect(comboNsfwFilter, QOverload<int>::of(&QComboBox::currentIndexChanged), dlg,
                [dlg, saveDiffusionSettings](int idx) {
                    if (idx > 0 && !g_nsfwFilterWarningShownThisSession) {
                        g_nsfwFilterWarningShownThisSession = true;
                        QMessageBox::warning(
                            dlg,
                            ComfyTr::tr("NSFW Filter Warning"),
                            ComfyTr::tr("The NSFW filter is a basic tool to exclude explicit content from generated images. It is NOT a guarantee and may not catch all inappropriate content. Please use responsibly and always review the generated images."));
                    }
                    saveDiffusionSettings();
                });

        diffusionLayout->addStretch();
        diffusionScroll->setWidget(diffusionInner);
        diffusionOuter->addWidget(diffusionScroll);
        stack->addWidget(diffusionPage);

        // Interface tab (index 3) — Python InterfaceSettings (settings.py L699–793)
        QWidget *interfacePage = new QWidget(dlg);
        QVBoxLayout *interfaceOuter = new QVBoxLayout(interfacePage);
        interfaceOuter->setContentsMargins(0, 0, 0, 0);
        QScrollArea *ifaceScroll = new QScrollArea(interfacePage);
        ifaceScroll->setWidgetResizable(true);
        ifaceScroll->setFrameShape(QFrame::NoFrame);
        ifaceScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        QWidget *ifaceInner = new QWidget();
        QVBoxLayout *interfaceLayout = new QVBoxLayout(ifaceInner);
        QLabel *ifaceHeading = new QLabel(ComfyTr::tr("Interface Settings"), ifaceInner);
        QFont ifaceHeadingFont = ifaceHeading->font();
        ifaceHeadingFont.setBold(true);
        ifaceHeadingFont.setPointSize(ifaceHeadingFont.pointSize() + 2);
        ifaceHeading->setFont(ifaceHeadingFont);
        interfaceLayout->addWidget(ifaceHeading);
        interfaceLayout->addSpacing(6);

        auto makeIfaceLabelColumn = [](QWidget *parent, const QString &title, const QString &description) -> QWidget * {
            auto *col = new QWidget(parent);
            auto *colLayout = new QVBoxLayout(col);
            colLayout->setContentsMargins(0, 0, 0, 0);
            colLayout->setSpacing(2);
            auto *titleLabel = new QLabel(title, col);
            QFont titleFont = titleLabel->font();
            titleFont.setBold(true);
            titleLabel->setFont(titleFont);
            colLayout->addWidget(titleLabel);
            if (!description.isEmpty()) {
                auto *descLabel = new QLabel(description, col);
                descLabel->setWordWrap(true);
                colLayout->addWidget(descLabel);
            }
            col->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
            return col;
        };
        auto addIfaceSpinRow = [makeIfaceLabelColumn](QWidget *parent, const QString &title, const QString &description,
                                                      QSpinBox **outSpin, int min, int max) -> QWidget * {
            auto *row = new QWidget(parent);
            auto *rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(0, 4, 0, 4);
            rowLayout->addWidget(makeIfaceLabelColumn(row, title, description), 1);
            auto *spin = new QSpinBox(row);
            spin->setMinimumWidth(100);
            spin->setRange(min, max);
            rowLayout->addWidget(spin, 0, Qt::AlignRight | Qt::AlignVCenter);
            *outSpin = spin;
            return row;
        };
        auto addIfaceSwitchRow = [makeIfaceLabelColumn](QWidget *parent, const QString &title, const QString &description,
                                                        const QString &onLabel, const QString &offLabel,
                                                        ComfySwitchWidget **outSwitch, QLabel **outStateLabel) -> QWidget * {
            auto *row = new QWidget(parent);
            auto *rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(0, 4, 0, 4);
            rowLayout->addWidget(makeIfaceLabelColumn(row, title, description), 1);
            auto *stateLabel = new QLabel(onLabel, row);
            auto *sw = new ComfySwitchWidget(row);
            rowLayout->addWidget(stateLabel, 0, Qt::AlignRight | Qt::AlignVCenter);
            rowLayout->addWidget(sw, 0, Qt::AlignRight | Qt::AlignVCenter);
            connect(sw, &QAbstractButton::toggled, row, [stateLabel, onLabel, offLabel](bool on) {
                stateLabel->setText(on ? onLabel : offLabel);
            });
            *outSwitch = sw;
            *outStateLabel = stateLabel;
            return row;
        };
        auto addIfaceComboRow = [makeIfaceLabelColumn](QWidget *parent, const QString &title, const QString &description,
                                                       QComboBox **outCombo) -> QWidget * {
            auto *row = new QWidget(parent);
            auto *rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(0, 4, 0, 4);
            rowLayout->addWidget(makeIfaceLabelColumn(row, title, description), 1);
            auto *combo = new QComboBox(row);
            combo->setMinimumWidth(230);
            rowLayout->addWidget(combo, 0, Qt::AlignRight | Qt::AlignVCenter);
            *outCombo = combo;
            return row;
        };

        QJsonObject ifaceSettings = ComfyUIUtils::loadSettingsJson();

        QComboBox *comboLanguage = nullptr;
        interfaceLayout->addWidget(addIfaceComboRow(
            ifaceInner,
            ComfyTr::tr("Language"),
            ComfyTr::tr("Interface language used by the plugin - requires restart!"),
            &comboLanguage));
        const QList<ComfyLanguageInfo> availableLangs = ComfyLocalization::instance().availableLanguages();
        for (const ComfyLanguageInfo &lang : availableLangs)
            comboLanguage->addItem(lang.name, lang.id);
        if (comboLanguage->count() == 0)
            comboLanguage->addItem(ComfyTr::tr("English"), QStringLiteral("en"));
        {
            QString curLang = ifaceSettings.value(QStringLiteral("interface_language")).toString();
            if (curLang.isEmpty())
                curLang = ifaceSettings.value(QStringLiteral("language")).toString();
            if (curLang.isEmpty())
                curLang = ComfyLocalization::instance().languageId();
            curLang = curLang.trimmed().toLower().replace(QLatin1Char('_'), QLatin1Char('-'));
            const int li = comboLanguage->findData(curLang);
            comboLanguage->setCurrentIndex(li >= 0 ? li : 0);
        }

        QComboBox *comboPromptTranslation = nullptr;
        interfaceLayout->addWidget(addIfaceComboRow(
            ifaceInner,
            ComfyTr::tr("Prompt Translation"),
            ComfyTr::tr("Translate text prompts from the selected language to English"),
            &comboPromptTranslation));
        m_d->settingsPromptTranslationCombo = comboPromptTranslation;

        QSpinBox *spinPromptLines = nullptr;
        interfaceLayout->addWidget(addIfaceSpinRow(
            ifaceInner,
            ComfyTr::tr("Prompt Line Count"),
            ComfyTr::tr("Size of the text editor for image descriptions"),
            &spinPromptLines,
            1,
            10));
        spinPromptLines->setValue(ifaceSettings.value(QStringLiteral("prompt_line_count")).toInt(2));

        ComfySwitchWidget *switchShowNegative = nullptr;
        QLabel *labelShowNegativeState = nullptr;
        interfaceLayout->addWidget(addIfaceSwitchRow(
            ifaceInner,
            ComfyTr::tr("Negative Prompt"),
            ComfyTr::tr("Show text editor to describe things to avoid"),
            ComfyTr::tr("Show"),
            ComfyTr::tr("Hide"),
            &switchShowNegative,
            &labelShowNegativeState));
        switchShowNegative->setChecked(ifaceSettings.value(QStringLiteral("show_negative_prompt")).toBool(false));
        labelShowNegativeState->setText(switchShowNegative->isChecked() ? ComfyTr::tr("Show") : ComfyTr::tr("Hide"));

        ComfySwitchWidget *switchShowSteps = nullptr;
        QLabel *labelShowStepsState = nullptr;
        interfaceLayout->addWidget(addIfaceSwitchRow(
            ifaceInner,
            ComfyTr::tr("Show Steps"),
            ComfyTr::tr("Display the number of steps to be evaluated in the weights box."),
            ComfyTr::tr("On"),
            ComfyTr::tr("Off"),
            &switchShowSteps,
            &labelShowStepsState));
        switchShowSteps->setChecked(ifaceSettings.value(QStringLiteral("show_steps")).toBool(false));
        labelShowStepsState->setText(switchShowSteps->isChecked() ? ComfyTr::tr("On") : ComfyTr::tr("Off"));

        QSpinBox *spinRecentStyles = nullptr;
        interfaceLayout->addWidget(addIfaceSpinRow(
            ifaceInner,
            ComfyTr::tr("Recent Styles"),
            ComfyTr::tr("Number of most recently used styles to show at the top of the style list"),
            &spinRecentStyles,
            0,
            10));
        spinRecentStyles->setValue(ifaceSettings.value(QStringLiteral("recent_styles_count")).toInt(4));

        // Tag auto-completion — bundled tag CSV lists (Danbooru / e621 + NSFW variants)
        QLabel *tagStateLabel = nullptr;
        QCheckBox *chkTagDanbooru = nullptr;
        QCheckBox *chkTagDanbooruNsfw = nullptr;
        QCheckBox *chkTagE621 = nullptr;
        QCheckBox *chkTagE621Nsfw = nullptr;
        {
            QSet<QString> selectedTagStems;
            const QJsonArray tf = ifaceSettings.value(QStringLiteral("tag_files")).toArray();
            if (tf.isEmpty()) {
                selectedTagStems.insert(QStringLiteral("Danbooru"));
                selectedTagStems.insert(QStringLiteral("e621"));
            } else {
                for (const QJsonValue &v : tf)
                    selectedTagStems.insert(v.toString());
            }

            auto *tagRow = new QWidget(ifaceInner);
            auto *tagRowLayout = new QHBoxLayout(tagRow);
            tagRowLayout->setContentsMargins(0, 4, 0, 4);
            tagRowLayout->addWidget(makeIfaceLabelColumn(
                tagRow,
                ComfyTr::tr("Tag Auto-Completion"),
                ComfyTr::tr("Enable text completion for tags from the selected files")),
                                   1);
            tagStateLabel = new QLabel(ComfyTr::tr("Disabled"), tagRow);
            tagRowLayout->addWidget(tagStateLabel, 0, Qt::AlignRight | Qt::AlignVCenter);
            interfaceLayout->addWidget(tagRow);

            auto *tagListRow = new QWidget(ifaceInner);
            auto *tagListLayout = new QHBoxLayout(tagListRow);
            tagListLayout->setContentsMargins(16, 0, 0, 4);
            chkTagDanbooru = new QCheckBox(ComfyTr::tr("Danbooru"), tagListRow);
            chkTagDanbooru->setProperty("tagStem", QStringLiteral("Danbooru"));
            chkTagDanbooruNsfw = new QCheckBox(ComfyTr::tr("Danbooru NSFW"), tagListRow);
            chkTagDanbooruNsfw->setProperty("tagStem", QStringLiteral("Danbooru NSFW"));
            chkTagE621 = new QCheckBox(ComfyTr::tr("e621"), tagListRow);
            chkTagE621->setProperty("tagStem", QStringLiteral("e621"));
            chkTagE621Nsfw = new QCheckBox(ComfyTr::tr("e621 NSFW"), tagListRow);
            chkTagE621Nsfw->setProperty("tagStem", QStringLiteral("e621 NSFW"));
            chkTagDanbooru->setChecked(selectedTagStems.contains(QStringLiteral("Danbooru")));
            chkTagDanbooruNsfw->setChecked(selectedTagStems.contains(QStringLiteral("Danbooru NSFW")));
            chkTagE621->setChecked(selectedTagStems.contains(QStringLiteral("e621")));
            chkTagE621Nsfw->setChecked(selectedTagStems.contains(QStringLiteral("e621 NSFW")));
            tagListLayout->addWidget(chkTagDanbooru);
            tagListLayout->addWidget(chkTagDanbooruNsfw);
            tagListLayout->addWidget(chkTagE621);
            tagListLayout->addWidget(chkTagE621Nsfw);
            tagListLayout->addStretch();
            interfaceLayout->addWidget(tagListRow);

            const auto syncTagStateLabel = [tagStateLabel, chkTagDanbooru, chkTagDanbooruNsfw, chkTagE621, chkTagE621Nsfw]() {
                const bool any = chkTagDanbooru->isChecked() || chkTagDanbooruNsfw->isChecked()
                    || chkTagE621->isChecked() || chkTagE621Nsfw->isChecked();
                tagStateLabel->setText(any ? ComfyTr::tr("Enabled") : ComfyTr::tr("Disabled"));
            };
            syncTagStateLabel();

            QPushButton *btnLookNewTagFiles = new QPushButton(ComfyTr::tr("Look for new tag files"), ifaceInner);
            btnLookNewTagFiles->setIcon(ComfyTheme::icon(QStringLiteral("reset")));
            QPushButton *btnOpenTagFolder = new QPushButton(
                ComfyTr::tr("Open folder where custom tag files can be placed"),
                ifaceInner);
            btnOpenTagFolder->setIcon(ComfyTheme::icon(QStringLiteral("root")));
            auto *tagActionRow = new QWidget(ifaceInner);
            auto *tagActionLayout = new QHBoxLayout(tagActionRow);
            tagActionLayout->setContentsMargins(16, 0, 0, 4);
            tagActionLayout->addWidget(btnLookNewTagFiles);
            tagActionLayout->addWidget(btnOpenTagFolder);
            tagActionLayout->addStretch();
            interfaceLayout->addWidget(tagActionRow);

            connect(btnLookNewTagFiles, &QPushButton::clicked, this, [this, syncTagStateLabel](bool) {
                refreshPromptTagCompleter();
                syncTagStateLabel();
            });
            connect(btnOpenTagFolder, &QPushButton::clicked, dlg, [](bool) {
                QDesktopServices::openUrl(QUrl::fromLocalFile(ComfyUIUtils::tagsStorageDir()));
            });
        }

        QComboBox *comboFinishedAction = nullptr;
        interfaceLayout->addWidget(addIfaceComboRow(
            ifaceInner,
            ComfyTr::tr("Finished Generation"),
            ComfyTr::tr("Action to take when an image generation job finishes"),
            &comboFinishedAction));
        comboFinishedAction->addItem(ComfyTr::tr("Do Nothing"), QStringLiteral("none"));
        comboFinishedAction->addItem(ComfyTr::tr("Preview"), QStringLiteral("preview"));
        comboFinishedAction->addItem(ComfyTr::tr("Apply"), QStringLiteral("apply"));
        {
            QString finAction = ifaceSettings.value(QStringLiteral("generation_finished_action")).toString();
            if (finAction.isEmpty())
                finAction = QStringLiteral("preview");
            const int finIdx = comboFinishedAction->findData(finAction);
            comboFinishedAction->setCurrentIndex(finIdx >= 0 ? finIdx : 1);
        }

        QComboBox *comboApplyBehavior = nullptr;
        interfaceLayout->addWidget(addIfaceComboRow(
            ifaceInner,
            ComfyTr::tr("Apply Behavior"),
            ComfyTr::tr("Choose how result images are applied to the canvas (generation workspaces)"),
            &comboApplyBehavior));
        comboApplyBehavior->addItem(ComfyTr::tr("Modify active layer"), QStringLiteral("replace"));
        comboApplyBehavior->addItem(ComfyTr::tr("New layer on top"), QStringLiteral("layer"));
        comboApplyBehavior->addItem(ComfyTr::tr("New layer above active"), QStringLiteral("layer_active"));
        {
            QString applyBeh = ifaceSettings.value(QStringLiteral("apply_behavior")).toString();
            if (applyBeh.isEmpty())
                applyBeh = QStringLiteral("layer");
            const int applyIdx = comboApplyBehavior->findData(applyBeh);
            comboApplyBehavior->setCurrentIndex(applyIdx >= 0 ? applyIdx : 1);
        }

        QComboBox *comboApplyRegionBehavior = new QComboBox(ifaceInner);
        comboApplyRegionBehavior->setMinimumWidth(230);
        comboApplyRegionBehavior->addItem(ComfyTr::tr("Do not update regions"), QStringLiteral("none"));
        comboApplyRegionBehavior->addItem(ComfyTr::tr("Modify region layers"), QStringLiteral("replace"));
        comboApplyRegionBehavior->addItem(ComfyTr::tr("Layer group"), QStringLiteral("layer_group"));
        comboApplyRegionBehavior->addItem(ComfyTr::tr("Layer group + mask"), QStringLiteral("transparency_mask"));
        comboApplyRegionBehavior->addItem(ComfyTr::tr("Layer group (don't hide)"), QStringLiteral("no_hide"));
        {
            QString savedRegionBehavior = ifaceSettings.value(QStringLiteral("apply_region_behavior")).toString();
            if (savedRegionBehavior.isEmpty())
                savedRegionBehavior = QStringLiteral("layer_group");
            const int regionIdx = comboApplyRegionBehavior->findData(savedRegionBehavior);
            comboApplyRegionBehavior->setCurrentIndex(regionIdx >= 0 ? regionIdx : 2);
        }
        comboApplyRegionBehavior->setToolTip(
            ComfyTr::tr("When applying a result that was generated with regions, how to place the result per region."));
        {
            auto *regionRow = new QWidget(ifaceInner);
            auto *regionLayout = new QHBoxLayout(regionRow);
            regionLayout->setContentsMargins(0, 4, 0, 4);
            regionLayout->addStretch(1);
            regionLayout->addWidget(comboApplyRegionBehavior, 0, Qt::AlignRight);
            interfaceLayout->addWidget(regionRow);
        }

        QComboBox *comboApplyBehaviorLive = nullptr;
        interfaceLayout->addWidget(addIfaceComboRow(
            ifaceInner,
            ComfyTr::tr("Apply Behavior (Live)"),
            ComfyTr::tr("Choose how result images are applied to the canvas in Live mode"),
            &comboApplyBehaviorLive));
        comboApplyBehaviorLive->addItem(ComfyTr::tr("Modify active layer"), QStringLiteral("replace"));
        comboApplyBehaviorLive->addItem(ComfyTr::tr("New layer on top"), QStringLiteral("layer"));
        comboApplyBehaviorLive->addItem(ComfyTr::tr("New layer above active"), QStringLiteral("layer_active"));
        {
            QString applyBehLive = ifaceSettings.value(QStringLiteral("apply_behavior_live")).toString();
            if (applyBehLive.isEmpty())
                applyBehLive = QStringLiteral("replace");
            const int applyLiveIdx = comboApplyBehaviorLive->findData(applyBehLive);
            comboApplyBehaviorLive->setCurrentIndex(applyLiveIdx >= 0 ? applyLiveIdx : 0);
        }

        QComboBox *comboApplyRegionBehaviorLive = new QComboBox(ifaceInner);
        comboApplyRegionBehaviorLive->setMinimumWidth(230);
        comboApplyRegionBehaviorLive->addItem(ComfyTr::tr("Do not update regions"), QStringLiteral("none"));
        comboApplyRegionBehaviorLive->addItem(ComfyTr::tr("Modify region layers"), QStringLiteral("replace"));
        comboApplyRegionBehaviorLive->addItem(ComfyTr::tr("Layer group"), QStringLiteral("layer_group"));
        comboApplyRegionBehaviorLive->addItem(ComfyTr::tr("Layer group + mask"), QStringLiteral("transparency_mask"));
        comboApplyRegionBehaviorLive->addItem(ComfyTr::tr("Layer group (don't hide)"), QStringLiteral("no_hide"));
        {
            QString savedRegionBehaviorLive = ifaceSettings.value(QStringLiteral("apply_region_behavior_live")).toString();
            if (savedRegionBehaviorLive.isEmpty())
                savedRegionBehaviorLive = QStringLiteral("replace");
            const int regionLiveIdx = comboApplyRegionBehaviorLive->findData(savedRegionBehaviorLive);
            comboApplyRegionBehaviorLive->setCurrentIndex(regionLiveIdx >= 0 ? regionLiveIdx : 1);
        }
        comboApplyRegionBehaviorLive->setToolTip(
            ComfyTr::tr("Same as apply region behavior, used when the Live workspace is active."));
        {
            auto *regionLiveRow = new QWidget(ifaceInner);
            auto *regionLiveLayout = new QHBoxLayout(regionLiveRow);
            regionLiveLayout->setContentsMargins(0, 4, 0, 4);
            regionLiveLayout->addStretch(1);
            regionLiveLayout->addWidget(comboApplyRegionBehaviorLive, 0, Qt::AlignRight);
            interfaceLayout->addWidget(regionLiveRow);
        }

        ComfySwitchWidget *switchNewSeedAfterApply = nullptr;
        QLabel *labelNewSeedState = nullptr;
        interfaceLayout->addWidget(addIfaceSwitchRow(
            ifaceInner,
            ComfyTr::tr("Live: New Seed after Apply"),
            ComfyTr::tr("Pick a new seed after copying the result to the canvas in Live mode"),
            ComfyTr::tr("On"),
            ComfyTr::tr("Off"),
            &switchNewSeedAfterApply,
            &labelNewSeedState));
        switchNewSeedAfterApply->setChecked(ifaceSettings.value(QStringLiteral("new_seed_after_apply")).toBool(false));
        labelNewSeedState->setText(switchNewSeedAfterApply->isChecked() ? ComfyTr::tr("On") : ComfyTr::tr("Off"));

        QComboBox *comboSaveFormat = nullptr;
        interfaceLayout->addWidget(addIfaceComboRow(
            ifaceInner,
            ComfyTr::tr("Save Image Format"),
            ComfyTr::tr("File format for saved images from thumbnails."),
            &comboSaveFormat));
        comboSaveFormat->addItem(ComfyTr::tr("PNG (fast)"), QStringLiteral("png_small"));
        comboSaveFormat->addItem(ComfyTr::tr("PNG"), QStringLiteral("png"));
        comboSaveFormat->addItem(ComfyTr::tr("WebP"), QStringLiteral("webp"));
        comboSaveFormat->addItem(ComfyTr::tr("WebP (lossless)"), QStringLiteral("webp_lossless"));
        comboSaveFormat->addItem(ComfyTr::tr("JPEG"), QStringLiteral("jpeg"));
        {
            QString sf = ifaceSettings.value(QStringLiteral("save_image_format")).toString();
            if (sf.isEmpty())
                sf = QStringLiteral("png_small");
            int sfi = comboSaveFormat->findData(sf);
            if (sfi < 0 && sf == QLatin1String("png"))
                sfi = comboSaveFormat->findData(QStringLiteral("png"));
            comboSaveFormat->setCurrentIndex(sfi >= 0 ? sfi : 0);
        }

        ComfySwitchWidget *switchSaveMeta = nullptr;
        QLabel *labelSaveMetaState = nullptr;
        interfaceLayout->addWidget(addIfaceSwitchRow(
            ifaceInner,
            ComfyTr::tr("Save Image Metadata"),
            ComfyTr::tr("When saving generated images from thumbnails, include metadata in the PNG"),
            ComfyTr::tr("On"),
            ComfyTr::tr("Off"),
            &switchSaveMeta,
            &labelSaveMetaState));
        switchSaveMeta->setChecked(ifaceSettings.value(QStringLiteral("save_image_metadata")).toBool(false));
        labelSaveMetaState->setText(switchSaveMeta->isChecked() ? ComfyTr::tr("On") : ComfyTr::tr("Off"));

        ComfySwitchWidget *switchDumpWorkflow = nullptr;
        QLabel *labelDumpWorkflowState = nullptr;
        interfaceLayout->addWidget(addIfaceSwitchRow(
            ifaceInner,
            ComfyTr::tr("Dump Workflow"),
            ComfyTr::tr("Write latest ComfyUI prompt to the log folder for test & debug"),
            ComfyTr::tr("On"),
            ComfyTr::tr("Off"),
            &switchDumpWorkflow,
            &labelDumpWorkflowState));
        const bool dumpOn = ifaceSettings.value(QStringLiteral("debug_dump_workflow")).toBool(false)
            || ifaceSettings.value(QStringLiteral("dump_workflow")).toBool(false);
        switchDumpWorkflow->setChecked(dumpOn);
        labelDumpWorkflowState->setText(dumpOn ? ComfyTr::tr("On") : ComfyTr::tr("Off"));

        auto updateSaveFormatSideEffects = [switchSaveMeta, labelSaveMetaState, comboSaveFormat]() {
            const QString f = comboSaveFormat->currentData().toString();
            const bool pngFmt = (f == QLatin1String("png") || f == QLatin1String("png_small"));
            switchSaveMeta->setEnabled(pngFmt);
            if (!pngFmt) {
                switchSaveMeta->setChecked(false);
                labelSaveMetaState->setText(ComfyTr::tr("Off"));
            }
        };
        updateSaveFormatSideEffects();

        auto saveIfaceSettings = [this, comboFinishedAction, comboApplyBehavior, comboApplyBehaviorLive, switchNewSeedAfterApply,
                                  comboApplyRegionBehavior, comboApplyRegionBehaviorLive, comboLanguage,
                                  comboPromptTranslation, spinPromptLines, switchShowNegative, switchShowSteps,
                                  spinRecentStyles, comboSaveFormat, switchSaveMeta, switchDumpWorkflow,
                                  chkTagDanbooru, chkTagDanbooruNsfw, chkTagE621, chkTagE621Nsfw, tagStateLabel]() {
            QJsonObject s = ComfyUIUtils::loadSettingsJson();
            const QString langId = comboLanguage->currentData().toString();
            s.insert(QStringLiteral("interface_language"), langId);
            s.insert(QStringLiteral("language"), langId);
            QString pt = comboPromptTranslation->currentData().toString();
            if (pt.isEmpty())
                pt = QStringLiteral("disabled");
            s.insert(QStringLiteral("prompt_translation"), pt);
            s.insert(QStringLiteral("prompt_line_count"), spinPromptLines->value());
            s.insert(QStringLiteral("show_negative_prompt"), switchShowNegative->isChecked());
            s.insert(QStringLiteral("show_steps"), switchShowSteps->isChecked());
            s.insert(QStringLiteral("recent_styles_count"), spinRecentStyles->value());
            QJsonArray tagFiles;
            const auto appendIfChecked = [&tagFiles](QCheckBox *cb) {
                if (cb && cb->isChecked()) {
                    const QString stem = cb->property("tagStem").toString();
                    tagFiles.append(stem.isEmpty() ? cb->text() : stem);
                }
            };
            appendIfChecked(chkTagDanbooru);
            appendIfChecked(chkTagDanbooruNsfw);
            appendIfChecked(chkTagE621);
            appendIfChecked(chkTagE621Nsfw);
            s.insert(QStringLiteral("tag_files"), tagFiles);
            if (tagStateLabel) {
                tagStateLabel->setText(tagFiles.isEmpty() ? ComfyTr::tr("Disabled") : ComfyTr::tr("Enabled"));
            }
            s.insert(QStringLiteral("save_image_format"), comboSaveFormat->currentData().toString());
            s.insert(QStringLiteral("save_image_metadata"), switchSaveMeta->isChecked());
            s.insert(QStringLiteral("debug_dump_workflow"), switchDumpWorkflow->isChecked());
            s.remove(QStringLiteral("dump_workflow"));
            s.insert(QStringLiteral("generation_finished_action"), comboFinishedAction->currentData().toString());
            s.insert(QStringLiteral("apply_behavior"), comboApplyBehavior->currentData().toString());
            s.insert(QStringLiteral("apply_behavior_live"), comboApplyBehaviorLive->currentData().toString());
            s.insert(QStringLiteral("new_seed_after_apply"), switchNewSeedAfterApply->isChecked());
            s.insert(QStringLiteral("apply_region_behavior"), comboApplyRegionBehavior->currentData().toString());
            s.insert(QStringLiteral("apply_region_behavior_live"), comboApplyRegionBehaviorLive->currentData().toString());
            ComfyUIUtils::saveSettingsJson(s);
            applyInterfaceAppearanceSettings();
            refreshPromptTagCompleter();
            persistDocumentDefaultsToSettings();
        };

        connect(comboLanguage, QOverload<int>::of(&QComboBox::currentIndexChanged), dlg,
                [comboLanguage, saveIfaceSettings](int) {
                    const QString newId = comboLanguage->currentData().toString();
                    if (newId != ComfyLocalization::instance().languageId()) {
                        QMessageBox::information(
                            comboLanguage,
                            ComfyTr::tr("Language"),
                            ComfyTr::tr("Restart Krita to apply the new interface language."));
                    }
                    saveIfaceSettings();
                });
        connect(comboPromptTranslation, QOverload<int>::of(&QComboBox::currentIndexChanged), dlg, saveIfaceSettings);
        connect(spinPromptLines, QOverload<int>::of(&QSpinBox::valueChanged), dlg, saveIfaceSettings);
        connect(switchShowNegative, &QAbstractButton::toggled, dlg, saveIfaceSettings);
        connect(switchShowSteps, &QAbstractButton::toggled, dlg, saveIfaceSettings);
        connect(spinRecentStyles, QOverload<int>::of(&QSpinBox::valueChanged), dlg, saveIfaceSettings);
        connect(comboSaveFormat, QOverload<int>::of(&QComboBox::currentIndexChanged), dlg, [saveIfaceSettings, updateSaveFormatSideEffects](int) {
            updateSaveFormatSideEffects();
            saveIfaceSettings();
        });
        connect(switchSaveMeta, &QAbstractButton::toggled, dlg, saveIfaceSettings);
        connect(switchDumpWorkflow, &QAbstractButton::toggled, dlg, saveIfaceSettings);
        connect(comboFinishedAction, QOverload<int>::of(&QComboBox::currentIndexChanged), dlg, saveIfaceSettings);
        connect(comboApplyBehavior, QOverload<int>::of(&QComboBox::currentIndexChanged), dlg, saveIfaceSettings);
        connect(comboApplyBehaviorLive, QOverload<int>::of(&QComboBox::currentIndexChanged), dlg, saveIfaceSettings);
        connect(switchNewSeedAfterApply, &QAbstractButton::toggled, dlg, saveIfaceSettings);
        connect(comboApplyRegionBehavior, QOverload<int>::of(&QComboBox::currentIndexChanged), dlg, saveIfaceSettings);
        connect(comboApplyRegionBehaviorLive, QOverload<int>::of(&QComboBox::currentIndexChanged), dlg, saveIfaceSettings);
        connect(chkTagDanbooru, &QCheckBox::toggled, dlg, saveIfaceSettings);
        connect(chkTagDanbooruNsfw, &QCheckBox::toggled, dlg, saveIfaceSettings);
        connect(chkTagE621, &QCheckBox::toggled, dlg, saveIfaceSettings);
        connect(chkTagE621Nsfw, &QCheckBox::toggled, dlg, saveIfaceSettings);

        refreshInterfacePromptTranslationCombo();
        {
            QString pt = ifaceSettings.value(QStringLiteral("prompt_translation")).toString().trimmed();
            if (pt.isEmpty() || pt == QLatin1String("disabled"))
                comboPromptTranslation->setCurrentIndex(0);
            else {
                const int pti = comboPromptTranslation->findData(pt);
                if (pti >= 0)
                    comboPromptTranslation->setCurrentIndex(pti);
            }
        }

        interfaceLayout->addStretch();
        ifaceScroll->setWidget(ifaceInner);
        interfaceOuter->addWidget(ifaceScroll);
        stack->addWidget(interfacePage);

        // Performance tab (index 4) — Python PerformanceSettings (settings.py L826–937)
        QWidget *perfPage = new QWidget(dlg);
        QVBoxLayout *perfOuter = new QVBoxLayout(perfPage);
        perfOuter->setContentsMargins(0, 0, 0, 0);
        QScrollArea *perfScroll = new QScrollArea(perfPage);
        perfScroll->setWidgetResizable(true);
        perfScroll->setFrameShape(QFrame::NoFrame);
        perfScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        QWidget *perfInner = new QWidget();
        QVBoxLayout *perfLayout = new QVBoxLayout(perfInner);
        QLabel *perfHeading = new QLabel(ComfyTr::tr("Performance Settings"), perfInner);
        QFont perfHeadingFont = perfHeading->font();
        perfHeadingFont.setBold(true);
        perfHeadingFont.setPointSize(perfHeadingFont.pointSize() + 2);
        perfHeading->setFont(perfHeadingFont);
        perfLayout->addWidget(perfHeading);
        perfLayout->addSpacing(6);

        auto makePerfLabelColumn = [](QWidget *parent, const QString &title, const QString &description) -> QWidget * {
            auto *col = new QWidget(parent);
            auto *colLayout = new QVBoxLayout(col);
            colLayout->setContentsMargins(0, 0, 0, 0);
            colLayout->setSpacing(2);
            auto *titleLabel = new QLabel(title, col);
            QFont titleFont = titleLabel->font();
            titleFont.setBold(true);
            titleLabel->setFont(titleFont);
            colLayout->addWidget(titleLabel);
            if (!description.isEmpty()) {
                auto *descLabel = new QLabel(description, col);
                descLabel->setWordWrap(true);
                colLayout->addWidget(descLabel);
            }
            col->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
            return col;
        };
        auto addPerfHistoryBlock = [makePerfLabelColumn](QWidget *parent, const QString &title, const QString &description,
                                                         QSpinBox **outSpin, QLabel **outUsageLabel, int min, int max,
                                                         int step) -> QWidget * {
            auto *block = new QWidget(parent);
            auto *blockLayout = new QVBoxLayout(block);
            blockLayout->setContentsMargins(0, 4, 0, 4);
            blockLayout->setSpacing(4);
            blockLayout->addWidget(makePerfLabelColumn(block, title, description));
            auto *usageRow = new QWidget(block);
            auto *usageLayout = new QHBoxLayout(usageRow);
            usageLayout->setContentsMargins(0, 0, 0, 0);
            auto *spin = new QSpinBox(usageRow);
            spin->setRange(min, max);
            spin->setSingleStep(step);
            spin->setSuffix(ComfyTr::tr(" MB"));
            auto *usageLabel = new QLabel(ComfyTr::tr("Currently using %1 MB", QStringLiteral("0.0")), usageRow);
            usageLabel->setStyleSheet(QStringLiteral("color: green; font-style: italic;"));
            usageLayout->addWidget(spin);
            usageLayout->addWidget(usageLabel, 1);
            blockLayout->addWidget(usageRow);
            *outSpin = spin;
            *outUsageLabel = usageLabel;
            return block;
        };
        auto addPerfSliderRow = [makePerfLabelColumn](QWidget *parent, const QString &title, const QString &description,
                                                    QSlider **outSlider, QLabel **outValueLabel, int min, int max,
                                                    const QString &valueFormat) -> QWidget * {
            auto *row = new QWidget(parent);
            auto *rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(0, 4, 0, 4);
            rowLayout->addWidget(makePerfLabelColumn(row, title, description), 1);
            auto *sliderBox = new QWidget(row);
            auto *sliderLayout = new QHBoxLayout(sliderBox);
            sliderLayout->setContentsMargins(0, 0, 0, 0);
            auto *slider = new QSlider(Qt::Horizontal, sliderBox);
            slider->setMinimumWidth(200);
            slider->setMaximumWidth(300);
            slider->setRange(min, max);
            auto *valueLabel = new QLabel(sliderBox);
            const QFontMetrics fm(valueLabel->font());
            valueLabel->setMinimumWidth(fm.horizontalAdvance(QStringLiteral("1.5×")));
            valueLabel->setText(valueFormat);
            sliderLayout->addWidget(slider);
            sliderLayout->addWidget(valueLabel);
            rowLayout->addWidget(sliderBox, 0, Qt::AlignRight | Qt::AlignVCenter);
            *outSlider = slider;
            *outValueLabel = valueLabel;
            return row;
        };
        auto addPerfSpinRow = [makePerfLabelColumn](QWidget *parent, const QString &title, const QString &description,
                                                    QSpinBox **outSpin, int min, int max, const QString &suffix) -> QWidget * {
            auto *row = new QWidget(parent);
            auto *rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(0, 4, 0, 4);
            rowLayout->addWidget(makePerfLabelColumn(row, title, description), 1);
            auto *spin = new QSpinBox(row);
            spin->setMinimumWidth(100);
            spin->setRange(min, max);
            if (!suffix.isEmpty())
                spin->setSuffix(suffix);
            rowLayout->addWidget(spin, 0, Qt::AlignRight | Qt::AlignVCenter);
            *outSpin = spin;
            return row;
        };
        auto addPerfSwitchRow = [makePerfLabelColumn](QWidget *parent, const QString &title, const QString &description,
                                                      const QString &onLabel, const QString &offLabel,
                                                      ComfySwitchWidget **outSwitch, QLabel **outStateLabel) -> QWidget * {
            auto *row = new QWidget(parent);
            auto *rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(0, 4, 0, 4);
            rowLayout->addWidget(makePerfLabelColumn(row, title, description), 1);
            auto *stateLabel = new QLabel(onLabel, row);
            auto *sw = new ComfySwitchWidget(row);
            rowLayout->addWidget(stateLabel, 0, Qt::AlignRight | Qt::AlignVCenter);
            rowLayout->addWidget(sw, 0, Qt::AlignRight | Qt::AlignVCenter);
            connect(sw, &QAbstractButton::toggled, row, [stateLabel, onLabel, offLabel](bool on) {
                stateLabel->setText(on ? onLabel : offLabel);
            });
            *outSwitch = sw;
            *outStateLabel = stateLabel;
            return row;
        };
        auto addPerfComboRow = [makePerfLabelColumn](QWidget *parent, const QString &title, const QString &description,
                                                     QComboBox **outCombo) -> QWidget * {
            auto *row = new QWidget(parent);
            auto *rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(0, 4, 0, 4);
            rowLayout->addWidget(makePerfLabelColumn(row, title, description), 1);
            auto *combo = new QComboBox(row);
            combo->setMinimumWidth(230);
            rowLayout->addWidget(combo, 0, Qt::AlignRight | Qt::AlignVCenter);
            *outCombo = combo;
            return row;
        };

        QJsonObject perfSettings = ComfyUIUtils::loadSettingsJson();
        QSpinBox *spinActiveHistoryMb = nullptr;
        QSpinBox *spinStoredHistoryMb = nullptr;
        perfLayout->addWidget(addPerfHistoryBlock(
            perfInner,
            ComfyTr::tr("Active History Size"),
            ComfyTr::tr("Main memory (RAM) used for the history of generated images."),
            &spinActiveHistoryMb,
            &m_d->labelHistoryUsageMb,
            5,
            20000,
            100));
        {
            int amb = perfSettings.value(QStringLiteral("history_size")).toInt(0);
            if (amb <= 0)
                amb = perfSettings.value(QStringLiteral("history_active_mb")).toInt(0);
            if (amb <= 0)
                amb = perfSettings.value(QStringLiteral("history_storage")).toInt(1000);
            spinActiveHistoryMb->setValue(qBound(5, amb, 20000));
            spinActiveHistoryMb->setToolTip(ComfyTr::tr("Oldest history entries are removed when over this limit."));
        }
        perfLayout->addWidget(addPerfHistoryBlock(
            perfInner,
            ComfyTr::tr("Stored History Size"),
            ComfyTr::tr("Memory used to store generated images in .kra files on disk."),
            &spinStoredHistoryMb,
            &m_d->labelStoredHistoryMb,
            5,
            2000,
            5));
        {
            int stored = perfSettings.value(QStringLiteral("history_storage")).toInt(0);
            if (stored <= 0)
                stored = perfSettings.value(QStringLiteral("history_document_storage_mb")).toInt(20);
            spinStoredHistoryMb->setValue(qBound(5, stored, 2000));
            spinStoredHistoryMb->setToolTip(ComfyTr::tr("Reserved for document-embedded history quota."));
        }

        QComboBox *comboPerfPreset = new QComboBox(perfInner);
        comboPerfPreset->setMinimumWidth(230);
        perfLayout->addWidget(makePerfLabelColumn(
            perfInner,
            ComfyTr::tr("Performance Preset"),
            ComfyTr::tr("Configures performance settings to match available hardware.")));
        m_d->labelPerfDevice = new QLabel(perfInner);
        m_d->labelPerfDevice->setWordWrap(true);
        m_d->labelPerfDevice->setStyleSheet(QStringLiteral("font-style: italic;"));
        m_d->labelPerfDevice->setText(m_d->comfyDeviceSummary.isEmpty()
                                          ? ComfyTr::tr("Device: (connect to server)")
                                          : m_d->comfyDeviceSummary);
        perfLayout->addWidget(m_d->labelPerfDevice);
        comboPerfPreset->addItem(ComfyTr::tr("Automatic"), QStringLiteral("auto"));
        comboPerfPreset->addItem(ComfyTr::tr("CPU"), QStringLiteral("cpu"));
        comboPerfPreset->addItem(ComfyTr::tr("GPU low (up to 6GB)"), QStringLiteral("low"));
        comboPerfPreset->addItem(ComfyTr::tr("GPU medium (6GB to 12GB)"), QStringLiteral("medium"));
        comboPerfPreset->addItem(ComfyTr::tr("GPU high (more than 12GB)"), QStringLiteral("high"));
        comboPerfPreset->addItem(ComfyTr::tr("Cloud"), QStringLiteral("cloud"));
        comboPerfPreset->addItem(ComfyTr::tr("Custom"), QStringLiteral("custom"));
        {
            QString pp = perfSettings.value(QStringLiteral("performance_preset")).toString();
            if (pp.isEmpty())
                pp = QStringLiteral("auto");
            const int ppi = comboPerfPreset->findData(pp);
            comboPerfPreset->setCurrentIndex(ppi >= 0 ? ppi : 0);
        }
        comboPerfPreset->setToolTip(ComfyTr::tr("Configures performance settings to match available hardware."));
        perfLayout->addWidget(comboPerfPreset, 0, Qt::AlignLeft);

        QWidget *customPerfWidget = new QWidget(perfInner);
        auto *customPerfLayout = new QVBoxLayout(customPerfWidget);
        customPerfLayout->setContentsMargins(8, 0, 0, 4);
        customPerfLayout->setSpacing(0);

        QSlider *sliderPerfBatch = nullptr;
        QLabel *labelPerfBatchVal = nullptr;
        customPerfLayout->addWidget(addPerfSliderRow(
            customPerfWidget,
            ComfyTr::tr("Maximum Batch Size"),
            ComfyTr::tr("Increase efficiency by generating multiple images at once."),
            &sliderPerfBatch,
            &labelPerfBatchVal,
            1,
            16,
            QStringLiteral("1")));
        sliderPerfBatch->setToolTip(ComfyTr::tr("Increase efficiency by generating multiple images at once."));

        QSlider *sliderPerfRes = nullptr;
        QLabel *labelPerfResVal = nullptr;
        customPerfLayout->addWidget(addPerfSliderRow(
            customPerfWidget,
            ComfyTr::tr("Resolution Multiplier"),
            ComfyTr::tr("Scaling factor for generation. Values below 1.0 improve performance for high resolution canvas."),
            &sliderPerfRes,
            &labelPerfResVal,
            3,
            15,
            QStringLiteral("1.0×")));
        sliderPerfRes->setToolTip(
            ComfyTr::tr("Scaling factor for generation. Values below 1.0 improve performance for high resolution canvas."));

        QSpinBox *spinMaxMp = nullptr;
        customPerfLayout->addWidget(addPerfSpinRow(
            customPerfWidget,
            ComfyTr::tr("Maximum Pixel Count"),
            ComfyTr::tr("Maximum resolution to generate images at, in megapixels (FullHD ~ 2MP, 4k ~ 8MP)."),
            &spinMaxMp,
            1,
            99,
            ComfyTr::tr(" MP")));
        {
            int maxMp = perfSettings.value(QStringLiteral("max_pixel_count")).toInt(0);
            if (maxMp <= 0)
                maxMp = perfSettings.value(QStringLiteral("max_pixel_count_mp")).toInt(8);
            spinMaxMp->setValue(qBound(1, maxMp, 99));
        }

        ComfySwitchWidget *switchTiledVae = nullptr;
        QLabel *labelTiledVaeState = nullptr;
        customPerfLayout->addWidget(addPerfSwitchRow(
            customPerfWidget,
            ComfyTr::tr("Tiled VAE"),
            ComfyTr::tr("Conserve memory by processing output images in smaller tiles."),
            ComfyTr::tr("Always"),
            ComfyTr::tr("Automatic"),
            &switchTiledVae,
            &labelTiledVaeState));
        {
            bool tiledAlways = perfSettings.value(QStringLiteral("tiled_vae")).toBool(false);
            if (!perfSettings.contains(QStringLiteral("tiled_vae"))) {
                QString tvm = perfSettings.value(QStringLiteral("tiled_vae_mode")).toString();
                if (tvm.isEmpty())
                    tvm = perfSettings.value(QStringLiteral("tiled_vae_always")).toBool(false)
                        ? QStringLiteral("always")
                        : QStringLiteral("automatic");
                tiledAlways = (tvm == QLatin1String("always"));
            }
            switchTiledVae->setChecked(tiledAlways);
            labelTiledVaeState->setText(tiledAlways ? ComfyTr::tr("Always") : ComfyTr::tr("Automatic"));
        }

        perfLayout->addWidget(customPerfWidget);

        ComfySwitchWidget *switchDynCache = nullptr;
        QLabel *labelDynCacheState = nullptr;
        perfLayout->addWidget(addPerfSwitchRow(
            perfInner,
            ComfyTr::tr("Dynamic Caching"),
            ComfyTr::tr("Re-use outputs of previous steps (First Block Cache) to speed up generation."),
            ComfyTr::tr("On"),
            ComfyTr::tr("Off"),
            &switchDynCache,
            &labelDynCacheState));
        switchDynCache->setChecked(perfSettings.value(QStringLiteral("dynamic_caching")).toBool(false));
        labelDynCacheState->setText(switchDynCache->isChecked() ? ComfyTr::tr("On") : ComfyTr::tr("Off"));
        switchDynCache->setToolTip(
            ComfyTr::tr("Re-use outputs of previous steps (First Block Cache) to speed up generation.\n\n"
                        "When enabled, the dock turns on common enable toggles on workflow nodes whose class names look like "
                        "First Block / Block / FB / Tea cache nodes (if those nodes are already in the graph)."));

        ComfySwitchWidget *switchMultiThread = nullptr;
        QLabel *labelMultiThreadState = nullptr;
        perfLayout->addWidget(addPerfSwitchRow(
            perfInner,
            ComfyTr::tr("Multi-Threading"),
            ComfyTr::tr("Perform certain plugin operations in background threads."),
            ComfyTr::tr("On"),
            ComfyTr::tr("Off"),
            &switchMultiThread,
            &labelMultiThreadState));
        switchMultiThread->setChecked(perfSettings.value(QStringLiteral("multi_threading")).toBool(true));
        labelMultiThreadState->setText(switchMultiThread->isChecked() ? ComfyTr::tr("On") : ComfyTr::tr("Off"));
        switchMultiThread->setToolTip(
            ComfyTr::tr("Perform certain plugin operations in background threads.\n\n"
                        "When enabled: Import Workflow reads and strips comments off the UI thread, and large \"Dump workflow\" "
                        "writes to last_comfy_prompt.json are written on a worker thread."));

        auto syncPerfSlidersFromDock = [this, sliderPerfBatch, labelPerfBatchVal, sliderPerfRes, labelPerfResVal, spinMaxMp,
                                        switchTiledVae, labelTiledVaeState, switchDynCache, labelDynCacheState,
                                        switchMultiThread, labelMultiThreadState]() {
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
            int maxMp = s.value(QStringLiteral("max_pixel_count")).toInt(0);
            if (maxMp <= 0)
                maxMp = s.value(QStringLiteral("max_pixel_count_mp")).toInt(8);
            spinMaxMp->setValue(qBound(1, maxMp, 99));
            spinMaxMp->blockSignals(false);
            bool tiledAlways = s.value(QStringLiteral("tiled_vae")).toBool(false);
            if (!s.contains(QStringLiteral("tiled_vae"))) {
                QString tvm = s.value(QStringLiteral("tiled_vae_mode")).toString();
                if (tvm.isEmpty())
                    tvm = s.value(QStringLiteral("tiled_vae_always")).toBool(false) ? QStringLiteral("always")
                                                                                  : QStringLiteral("automatic");
                tiledAlways = (tvm == QLatin1String("always"));
            }
            switchTiledVae->blockSignals(true);
            switchTiledVae->setChecked(tiledAlways);
            switchTiledVae->blockSignals(false);
            labelTiledVaeState->setText(tiledAlways ? ComfyTr::tr("Always") : ComfyTr::tr("Automatic"));
            switchDynCache->blockSignals(true);
            switchDynCache->setChecked(s.value(QStringLiteral("dynamic_caching")).toBool(false));
            switchDynCache->blockSignals(false);
            labelDynCacheState->setText(switchDynCache->isChecked() ? ComfyTr::tr("On") : ComfyTr::tr("Off"));
            switchMultiThread->blockSignals(true);
            switchMultiThread->setChecked(s.value(QStringLiteral("multi_threading")).toBool(true));
            switchMultiThread->blockSignals(false);
            labelMultiThreadState->setText(switchMultiThread->isChecked() ? ComfyTr::tr("On") : ComfyTr::tr("Off"));
            if (m_d->labelPerfDevice)
                m_d->labelPerfDevice->setText(m_d->comfyDeviceSummary.isEmpty()
                                                  ? ComfyTr::tr("Device: (connect to server)")
                                                  : m_d->comfyDeviceSummary);
        };
        syncPerfSlidersFromDock();
        auto updateCustomPerfEnabled = [comboPerfPreset, customPerfWidget]() {
            customPerfWidget->setEnabled(comboPerfPreset->currentData().toString() == QLatin1String("custom"));
        };
        updateCustomPerfEnabled();
        auto savePerfSettings = [this, comboPerfPreset, spinActiveHistoryMb, spinStoredHistoryMb, spinMaxMp, switchTiledVae,
                                 switchDynCache, switchMultiThread]() {
            QJsonObject s = ComfyUIUtils::loadSettingsJson();
            s.insert(QStringLiteral("performance_preset"), comboPerfPreset->currentData().toString());
            const int activeMb = spinActiveHistoryMb->value();
            const int storedMb = spinStoredHistoryMb->value();
            s.insert(QStringLiteral("history_size"), activeMb);
            s.insert(QStringLiteral("history_active_mb"), activeMb);
            s.insert(QStringLiteral("history_storage"), storedMb);
            s.insert(QStringLiteral("history_document_storage_mb"), storedMb);
            const int maxMp = spinMaxMp->value();
            s.insert(QStringLiteral("max_pixel_count"), maxMp);
            s.insert(QStringLiteral("max_pixel_count_mp"), maxMp);
            const bool tiledAlways = switchTiledVae->isChecked();
            s.insert(QStringLiteral("tiled_vae"), tiledAlways);
            const QString tiledMode = tiledAlways ? QStringLiteral("always") : QStringLiteral("automatic");
            s.insert(QStringLiteral("tiled_vae_mode"), tiledMode);
            s.insert(QStringLiteral("tiled_vae_always"), tiledAlways);
            s.insert(QStringLiteral("dynamic_caching"), switchDynCache->isChecked());
            s.insert(QStringLiteral("multi_threading"), switchMultiThread->isChecked());
            s.insert(QStringLiteral("batch_size"), m_d->spinBatchCount ? m_d->spinBatchCount->value() : 1);
            s.insert(QStringLiteral("resolution_multiplier"), m_d->resolutionMultiplier <= 0.0 ? 1.0 : m_d->resolutionMultiplier);
            ComfyUIUtils::saveSettingsJson(s);
            refreshQueueResolutionRowVisibility();
            updateUpscaleTargetSize();
        };
        connect(comboPerfPreset, QOverload<int>::of(&QComboBox::currentIndexChanged), dlg,
                [savePerfSettings, updateCustomPerfEnabled](int) {
                    updateCustomPerfEnabled();
                    savePerfSettings();
                });
        connect(comboPerfPreset, QOverload<int>::of(&QComboBox::activated), this,
                [this, comboPerfPreset, sliderPerfBatch, labelPerfBatchVal, sliderPerfRes, labelPerfResVal, spinMaxMp,
                 switchTiledVae, labelTiledVaeState, savePerfSettings, syncPerfSlidersFromDock](int) {
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
                    bool tiledAlways = false;
                    if (key == QLatin1String("cpu")) {
                        batch = 1;
                        maxMp = 2;
                    } else if (key == QLatin1String("low")) {
                        batch = 2;
                        maxMp = 2;
                        tiledAlways = true;
                    } else if (key == QLatin1String("medium")) {
                        batch = 4;
                        maxMp = 6;
                    } else if (key == QLatin1String("high")) {
                        batch = 6;
                        maxMp = 8;
                    } else if (key == QLatin1String("cloud")) {
                        batch = 8;
                        maxMp = 6;
                    } else {
                        savePerfSettings();
                        return;
                    }
                    if (m_d->spinBatchCount)
                        m_d->spinBatchCount->setValue(batch);
                    m_d->resolutionMultiplier = 1.0;
                    if (m_d->sliderResolutionMultiplier) {
                        m_d->sliderResolutionMultiplier->blockSignals(true);
                        m_d->sliderResolutionMultiplier->setValue(10);
                        m_d->sliderResolutionMultiplier->blockSignals(false);
                    }
                    if (m_d->labelResolutionMultiplier)
                        m_d->labelResolutionMultiplier->setText(QStringLiteral("1.0×"));
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
                    switchTiledVae->blockSignals(true);
                    switchTiledVae->setChecked(tiledAlways);
                    switchTiledVae->blockSignals(false);
                    labelTiledVaeState->setText(tiledAlways ? ComfyTr::tr("Always") : ComfyTr::tr("Automatic"));
                    savePerfSettings();
                });
        connect(spinActiveHistoryMb, QOverload<int>::of(&QSpinBox::valueChanged), this, [this, savePerfSettings](int) {
            savePerfSettings();
            pruneHistoryToStorageLimit();
            updateHistoryUsageLabel();
        });
        connect(spinStoredHistoryMb, QOverload<int>::of(&QSpinBox::valueChanged), dlg, savePerfSettings);
        connect(spinMaxMp, QOverload<int>::of(&QSpinBox::valueChanged), dlg, savePerfSettings);
        connect(switchTiledVae, &QAbstractButton::toggled, dlg, savePerfSettings);
        connect(switchDynCache, &QAbstractButton::toggled, dlg, savePerfSettings);
        connect(switchMultiThread, &QAbstractButton::toggled, dlg, savePerfSettings);
        connect(sliderPerfBatch, &QSlider::valueChanged, this, [this, labelPerfBatchVal, savePerfSettings](int v) {
            labelPerfBatchVal->setText(QString::number(v));
            if (m_d->spinBatchCount)
                m_d->spinBatchCount->setValue(v);
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

        perfLayout->addStretch();
        perfScroll->setWidget(perfInner);
        perfOuter->addWidget(perfScroll);
        stack->addWidget(perfPage);

        // Plugin tab removed on Android (per FAITHFUL_PORT note above). The
        // d-pointer's pluginTabLatestVersionLabel / pluginTabDownloadInstallButton
        // QPointers stay null and syncPluginUpdateUi() guards on each access, so
        // background update polling code paths still compile and run as no-ops.

        // Canonical refresh control for slotRefreshCheckpoints (hidden); Styles tab uses its own refresh button.
        if (m_d->btnRefreshCheckpoints) {
            m_d->btnRefreshCheckpoints->setParent(dlg);
            m_d->btnRefreshCheckpoints->hide();
        }

        // Footer (Restore Defaults, version text, Ok)
        QHBoxLayout *footerLayout = new QHBoxLayout();
        QPushButton *restoreButton = new QPushButton(ComfyTr::tr("Restore Defaults"), dlg);
        connect(restoreButton, &QPushButton::clicked, this, &ComfyUIRemoteDock::slotRestoreDefaults);
        footerLayout->addWidget(restoreButton);
        // §4.3 / §13.204: Plugin version from single source
        QLabel *footerVersion = new QLabel(ComfyTr::tr("Plugin version: %1", ComfyUIUtils::pluginVersion()), dlg);
        footerVersion->setAlignment(Qt::AlignCenter);
        footerVersion->setStyleSheet(QStringLiteral("color: gray; font-style: italic;"));
        footerLayout->addWidget(footerVersion, 1);
        QPushButton *okButton = new QPushButton(ComfyTr::tr("Ok"), dlg);
        okButton->setDefault(true);
        okButton->setAutoDefault(true);
        connect(okButton, &QPushButton::clicked, dlg, [dlg](bool) {
            KSharedConfig::openConfig()->sync();
            dlg->accept();
        });
        footerLayout->addWidget(okButton);
        mainLayout->addLayout(footerLayout);

        connect(navList, &QListWidget::currentRowChanged, stack, &QStackedWidget::setCurrentIndex);
        connect(navList, &QListWidget::currentRowChanged, this, [this, syncStylesFromDock, syncPerfSlidersFromDock](int row) {
            if (row == 0)
                refreshConnectionTabUi();
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

        refreshConnectionTabUi();
        refreshCustomWorkflowParameterPanel();
    }

    if (m_d->settingsDialog) {
        if (m_d->checkConfirmDiscardImage)
            m_d->checkConfirmDiscardImage->setChecked(KSharedConfig::openConfig()->group("ComfyUIRemote").readEntry("ConfirmDiscardImage", true));
        // Only external ComfyUI URLs are supported in this build; normalize legacy modes.
        if (m_d->connectionStack) {
            KConfigGroup cfg = KSharedConfig::openConfig()->group("ComfyUIRemote");
            if (cfg.readEntry("ServerMode", QStringLiteral("external")) != QLatin1String("external")) {
                cfg.writeEntry("ServerMode", QStringLiteral("external"));
                KSharedConfig::openConfig()->sync();
            }
            m_d->connectionStack->setCurrentIndex(0);
        }
        refreshCustomWorkflowParameterPanel();
        syncPluginUpdateUi();
        refreshConnectionTabUi();
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

    cfg.writeEntry(QStringLiteral("ServerMode"), QStringLiteral("external"));
    cfgPtr->sync();
    if (m_d->connectionStack) {
        m_d->connectionStack->setCurrentIndex(0);
    }
    syncPluginUpdateUi();
}
