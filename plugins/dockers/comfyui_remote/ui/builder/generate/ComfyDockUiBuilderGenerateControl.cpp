/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyDockUiBuilderGenerateInternal.h"

#include "ComfyUIRemoteDockShellInternal.h"
#include "ComfyUIRemoteDock.h"
#include "ComfyUIRemoteDockPrivate.h"
#include "ComfyUIUtils.h"
#include "ComfyTheme.h"
#include "ComfyWorkspaceSelectButton.h"
#include "ComfyPromptResizeHandle.h"
#include "ComfySwitchWidget.h"
#include "ComfyQueueButton.h"
#include "ComfyUIIntervalSlider.h"
#include "ComfyHistoryListWidget.h"
#include "ComfyRegionPromptWidget.h"
#include "ComfyRegionLink.h"

#include <QAbstractItemView>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QDesktopServices>
#include <QDoubleSpinBox>
#include <QFont>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QPixmap>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPointer>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSize>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStringListModel>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidgetAction>

#include <KSharedConfig>
#include <KConfigGroup>

#include <kis_annotation.h>
#include <kis_types.h>

using ComfyDockShellInternal::ComfyPromptPlainTextEdit;
using ComfyDockShellInternal::LiveSpinnerWidget;
using ComfyDockShellInternal::StrengthSpinBox;
using ComfyDockShellInternal::setComboCurrentItemData;


namespace ComfyDockUiBuilderGenerateInternal {

void buildControlPreviewSection(Workspace &ws)
{
    ComfyUIRemoteDock *dock = ws.dock;
    ComfyUIRemoteDock::Private *d = ws.d;
    DockShell &shell = *ws.shell;
    QVBoxLayout *genContentLayout = ws.genContentLayout;

    // §13.49 / §13.53: Control-layer timing range + server preprocessor preview (Generate workspace only)
    d->generate.controlPreviewGroupBox = new QGroupBox(ComfyTr::tr("Control preprocessor preview"), d->generate.genContentContainer);
    QVBoxLayout *cpLay = new QVBoxLayout(d->generate.controlPreviewGroupBox);
    d->generate.comboControlPreviewMode = new QComboBox(d->generate.controlPreviewGroupBox);
    d->generate.comboControlPreviewMode->setToolTip(
        ComfyTr::tr("Preprocessor applied to the current canvas image on the ComfyUI server (control.json modes)."));
    d->generate.comboControlPreviewMode->addItem(ComfyTr::tr("Depth"), QStringLiteral("depth"));
    d->generate.comboControlPreviewMode->addItem(ComfyTr::tr("Canny edge"), QStringLiteral("canny_edge"));
    d->generate.comboControlPreviewMode->addItem(ComfyTr::tr("Scribble"), QStringLiteral("scribble"));
    d->generate.comboControlPreviewMode->addItem(ComfyTr::tr("Line art"), QStringLiteral("line_art"));
    d->generate.comboControlPreviewMode->addItem(ComfyTr::tr("Soft edge"), QStringLiteral("soft_edge"));
    d->generate.comboControlPreviewMode->addItem(ComfyTr::tr("Hands"), QStringLiteral("hands"));
    d->generate.comboControlPreviewMode->addItem(ComfyTr::tr("Normal map"), QStringLiteral("normal"));
    d->generate.comboControlPreviewMode->addItem(ComfyTr::tr("Pose"), QStringLiteral("pose"));
    d->generate.comboControlPreviewMode->addItem(ComfyTr::tr("Segmentation"), QStringLiteral("segmentation"));
    cpLay->addWidget(new QLabel(ComfyTr::tr("Mode:"), d->generate.controlPreviewGroupBox));
    cpLay->addWidget(d->generate.comboControlPreviewMode);
    cpLay->addWidget(new QLabel(ComfyTr::tr("Control timing range (%):"), d->generate.controlPreviewGroupBox));
    d->generate.controlPreviewRangeSlider = new ComfyUIIntervalSlider(d->generate.controlPreviewGroupBox);
    d->generate.controlPreviewRangeSlider->setRange(0, 100);
    d->generate.controlPreviewRangeSlider->setToolTip(
        ComfyTr::tr("Low and high timing range for control strength (persisted; used when adding control layers)."));
    cpLay->addWidget(d->generate.controlPreviewRangeSlider);
    QObject::connect(d->generate.controlPreviewRangeSlider, &ComfyUIIntervalSlider::intervalChanged, dock, [](int low, int high) {
        KConfigGroup g = KSharedConfig::openConfig()->group("ComfyUIRemote");
        g.writeEntry("control_layer_timing_low_pct", low);
        g.writeEntry("control_layer_timing_high_pct", high);
    });
    d->generate.btnControlPreviewRun = new QPushButton(ComfyTr::tr("Run preprocessor preview"), d->generate.controlPreviewGroupBox);
    d->generate.btnControlPreviewRun->setToolTip(
        ComfyTr::tr("Upload the canvas, run the selected preprocessor on the server, and show the result below."));
    QObject::connect(d->generate.btnControlPreviewRun, &QPushButton::clicked, dock, &ComfyUIRemoteDock::slotControlPreviewRun);
    cpLay->addWidget(d->generate.btnControlPreviewRun);
    {
        QHBoxLayout *poseRow = new QHBoxLayout();
        poseRow->addWidget(new QLabel(ComfyTr::tr("Pose guide people:"), d->generate.controlPreviewGroupBox));
        d->generate.spinPoseGuidePeopleCount = new QSpinBox(d->generate.controlPreviewGroupBox);
        d->generate.spinPoseGuidePeopleCount->setRange(1, 3);
        d->generate.spinPoseGuidePeopleCount->setToolTip(
            ComfyTr::tr("Number of default stick figures to add (Pose.create_default people_count)."));
        QObject::connect(d->generate.spinPoseGuidePeopleCount, QOverload<int>::of(&QSpinBox::valueChanged), dock, [](int v) {
            KConfigGroup g = KSharedConfig::openConfig()->group(QStringLiteral("ComfyUIRemote"));
            g.writeEntry(QStringLiteral("pose_guide_people_count"), v);
        });
        poseRow->addWidget(d->generate.spinPoseGuidePeopleCount);
        d->generate.btnAddPoseGuide = new QPushButton(ComfyTr::tr("Add pose guide (vector layer)"), d->generate.controlPreviewGroupBox);
        d->generate.btnAddPoseGuide->setToolTip(
            ComfyTr::tr("Adds default stick-figure skeleton(s) to the selected vector layer and refreshes pose data from its SVG every 500 ms."));
        QObject::connect(d->generate.btnAddPoseGuide, &QPushButton::clicked, dock, &ComfyUIRemoteDock::slotAddPoseGuideToVectorLayer);
        poseRow->addWidget(d->generate.btnAddPoseGuide);
        poseRow->addStretch();
        cpLay->addLayout(poseRow);
    }
    d->generate.labelControlPreviewImage = new QLabel(d->generate.controlPreviewGroupBox);
    d->generate.labelControlPreviewImage->setMinimumSize(160, 160);
    d->generate.labelControlPreviewImage->setMaximumHeight(200);
    d->generate.labelControlPreviewImage->setAlignment(Qt::AlignCenter);
    d->generate.labelControlPreviewImage->setFrameShape(QFrame::Box);
    d->generate.labelControlPreviewImage->setScaledContents(false);
    d->generate.labelControlPreviewImage->setWordWrap(true);
    cpLay->addWidget(d->generate.labelControlPreviewImage);
    genContentLayout->addWidget(d->generate.controlPreviewGroupBox);
    d->generate.controlPreviewGroupBox->setVisible(d->comboWorkspace->currentIndex() == 0);
    dock->syncControlPreviewRangeFromSettings();
    dock->syncPoseGuidePeopleCountFromSettings();

}

} // namespace ComfyDockUiBuilderGenerateInternal
