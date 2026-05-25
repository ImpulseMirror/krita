/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef COMFYUI_POSE_LAYERS_H_
#define COMFYUI_POSE_LAYERS_H_

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QUuid>

#include <QImage>

#include <kis_types.h>

class KisDocument;
class KisShapeLayer;

// §13.98: Process-wide pose tracking for vector (shape) layers — timer-driven SVG refresh keyed by layer uuid.
class ComfyUIPoseLayers final : public QObject
{
    Q_OBJECT

public:
    static ComfyUIPoseLayers &instance();

    /// Register layer and insert default stick-figure SVG(s) for DWPreprocessor pose workflows (\p peopleCount 1–3, §13.98 Pose.create_default).
    bool addPoseCharacter(KisImageSP image, KisShapeLayerSP layer, KisDocument *document, int peopleCount = 1);

    /// Latest serialized SVG from the last poll for \p layerUuid (empty if unknown / not yet polled).
    QString lastPoseSvgForLayer(const QUuid &layerUuid) const;

    /// Rasterize cached pose SVG for \p layerUuid to \p size (transparent background). Empty if no cache.
    QImage rasterizedPoseImageForLayer(const QUuid &layerUuid, const QSize &size) const;

    /// Python layers.create_vector — new KisShapeLayer with \p svg, inserted above \p insertAbove when set.
    bool createVectorLayerFromSvg(KisImageSP image,
                                  KisDocument *document,
                                  const QString &layerName,
                                  const QString &svg,
                                  KisNodeSP insertAbove = KisNodeSP());

private:
    explicit ComfyUIPoseLayers(QObject *parent = nullptr);
    void slotPoll();
    void syncCachedSvg(KisShapeLayer *sl, KisImageSP image);

    struct TrackEntry {
        KisImageWSP image;
        QUuid layerUuid;
    };

    QTimer m_pollTimer;
    QList<TrackEntry> m_tracked;
    QHash<QUuid, QString> m_lastSvgByUuid;
};

#endif
