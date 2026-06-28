/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyUIPoseLayers.h"

#include <kis_group_layer.h>

#include <QBuffer>
#include <QDomDocument>
#include <QImage>
#include <QPainter>
#include <QSvgRenderer>

#include <algorithm>

#include <commands/KoShapeCreateCommand.h>
#include <KoShapeControllerBase.h>
#include <KoShapeContainer.h>
#include <kis_image.h>
#include <kis_layer_utils.h>
#include <kis_processing_applicator.h>
#include <kis_shape_layer.h>
#include <KoShape.h>
#include <SvgParser.h>
#include <SvgWriter.h>
#include <KoShapeLayer.h>

#include <KisDocument.h>
#include <commands/KoShapeCreateCommand.h>

namespace
{
void appendStickFigure(QString *out, int cx, int w, int h, int peopleCount)
{
    const int headR = qMax(6, qMin(w, h) / (14 + 2 * peopleCount));
    const int headCy = h / 7;
    const int shoulderY = headCy + headR + qMax(4, h / 40);
    const int bodyTop = shoulderY + headR;
    const int hipY = h * 52 / 100;
    const int footY = h * 92 / 100;
    const int armSpan = qBound(10, w / (2 + 2 * peopleCount), qMax(12, w / 8));
    const int armY = hipY - h / 12;
    const int lx = cx - armSpan / 2;
    const int rx = cx + armSpan / 2;
    out->append(QStringLiteral("<circle cx=\"%1\" cy=\"%2\" r=\"%3\"/>\n")
                    .arg(cx)
                    .arg(headCy)
                    .arg(headR));
    out->append(QStringLiteral("<line x1=\"%1\" y1=\"%2\" x2=\"%1\" y2=\"%3\"/>\n").arg(cx).arg(bodyTop).arg(hipY));
    out->append(QStringLiteral("<line x1=\"%1\" y1=\"%2\" x2=\"%3\" y2=\"%4\"/>\n")
                    .arg(cx)
                    .arg(shoulderY)
                    .arg(cx - armSpan)
                    .arg(armY));
    out->append(QStringLiteral("<line x1=\"%1\" y1=\"%2\" x2=\"%3\" y2=\"%4\"/>\n")
                    .arg(cx)
                    .arg(shoulderY)
                    .arg(cx + armSpan)
                    .arg(armY));
    out->append(QStringLiteral("<line x1=\"%1\" y1=\"%2\" x2=\"%3\" y2=\"%4\"/>\n").arg(cx).arg(hipY).arg(lx).arg(footY));
    out->append(QStringLiteral("<line x1=\"%1\" y1=\"%2\" x2=\"%3\" y2=\"%4\"/>\n").arg(cx).arg(hipY).arg(rx).arg(footY));
}

QString makeDefaultPoseSvg(const QRect &bounds, int peopleCount)
{
    const int n = qBound(1, peopleCount, 3);
    const int w = qMax(64, bounds.width());
    const int h = qMax(64, bounds.height());
    const int sw = qMax(2, qMin(w, h) / 200);
    QString inner;
    if (n == 1) {
        appendStickFigure(&inner, w / 2, w, h, n);
    } else if (n == 2) {
        appendStickFigure(&inner, w / 4, w, h, n);
        appendStickFigure(&inner, 3 * w / 4, w, h, n);
    } else {
        appendStickFigure(&inner, w / 6, w, h, n);
        appendStickFigure(&inner, w / 2, w, h, n);
        appendStickFigure(&inner, 5 * w / 6, w, h, n);
    }
    return QStringLiteral(
               "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
               "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%1\" height=\"%2\" viewBox=\"0 0 %1 %2\">\n"
               "<g fill=\"none\" stroke=\"#1a1a1a\" stroke-width=\"%3\" stroke-linecap=\"round\" stroke-linejoin=\"round\">\n"
               "%4"
               "</g>\n</svg>\n")
        .arg(w)
        .arg(h)
        .arg(sw)
        .arg(inner);
}

// §13.98 Pose.update(shapes, resolution): resolution is implicit in SvgWriter via image bounds and xRes (ppi),
// matching libkis VectorLayer::toSvg / document coordinate space.
QString shapeLayerToSvg(KisShapeLayer *layer, KisImageSP image)
{
    if (!layer || !image)
        return {};
    QList<KoShape *> shapes = layer->shapes();
    std::sort(shapes.begin(), shapes.end(), KoShape::compareShapeZIndex);
    const QSizeF sizeInPx = image->bounds().size();
    const QSizeF pageSize(sizeInPx.width() / image->xRes(), sizeInPx.height() / image->yRes());
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly);
    SvgWriter writer(shapes);
    writer.save(buffer, pageSize);
    buffer.close();
    return QString::fromUtf8(buffer.data());
}

} // namespace

ComfyUIPoseLayers &ComfyUIPoseLayers::instance()
{
    static ComfyUIPoseLayers s;
    return s;
}

ComfyUIPoseLayers::ComfyUIPoseLayers(QObject *parent)
    : QObject(parent)
{
    m_pollTimer.setInterval(500);
    connect(&m_pollTimer, &QTimer::timeout, this, &ComfyUIPoseLayers::slotPoll);
}

bool ComfyUIPoseLayers::addPoseCharacter(KisImageSP image, KisShapeLayerSP layer, KisDocument *document, int peopleCount)
{
    if (!image || !layer || !document || !document->shapeController())
        return false;

    const QString svg = makeDefaultPoseSvg(image->bounds(), peopleCount);
    QString errorMsg;
    int errorLine = 0;
    int errorColumn = 0;
    QDomDocument dom = SvgParser::createDocumentFromSvg(svg, &errorMsg, &errorLine, &errorColumn);
    if (dom.isNull())
        return false;

    auto *container = dynamic_cast<KoShapeContainer *>(layer.data());
    if (!container)
        return false;

    QSizeF fragmentSize;
    SvgParser parser(document->shapeController()->resourceManager());
    parser.setResolution(image->bounds(), image->xRes() * 72.0);
    QList<KoShape *> newShapes = parser.parseSvg(dom.documentElement(), &fragmentSize);
    if (newShapes.isEmpty())
        return false;

    KUndo2Command *cmd =
        new KoShapeCreateCommand(document->shapeController(), newShapes, container);
    KisProcessingApplicator::runSingleCommandStroke(image, cmd);
    image->waitForDone();

    const QUuid id = layer->uuid();
    bool found = false;
    for (const TrackEntry &e : m_tracked) {
        if (e.layerUuid == id && e.image.toStrongRef() == image) {
            found = true;
            break;
        }
    }
    if (!found) {
        TrackEntry te;
        te.image = image;
        te.layerUuid = id;
        m_tracked.append(te);
    }
    if (!m_pollTimer.isActive())
        m_pollTimer.start();
    syncCachedSvg(layer.data(), image);
    return true;
}

QString ComfyUIPoseLayers::lastPoseSvgForLayer(const QUuid &layerUuid) const
{
    return m_lastSvgByUuid.value(layerUuid);
}

QImage ComfyUIPoseLayers::rasterizedPoseImageForLayer(const QUuid &layerUuid, const QSize &size) const
{
    const QString svg = m_lastSvgByUuid.value(layerUuid);
    if (svg.isEmpty() || !size.isValid() || size.isEmpty())
        return {};
    QSvgRenderer renderer(svg.toUtf8());
    if (!renderer.isValid())
        return {};
    QImage img(size, QImage::Format_ARGB32);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);
    renderer.render(&p, QRectF(0, 0, size.width(), size.height()));
    return img;
}

void ComfyUIPoseLayers::syncCachedSvg(KisShapeLayer *sl, KisImageSP image)
{
    if (!sl || !image)
        return;
    const QString svg = shapeLayerToSvg(sl, image);
    if (!svg.isEmpty())
        m_lastSvgByUuid.insert(sl->uuid(), svg);
}

bool ComfyUIPoseLayers::createVectorLayerFromSvg(KisImageSP image,
                                                 KisDocument *document,
                                                 const QString &layerName,
                                                 const QString &svg,
                                                 KisNodeSP insertAbove)
{
    if (!image || !document || !document->shapeController() || svg.isEmpty())
        return false;
    QString errorMsg;
    int errorLine = 0;
    int errorColumn = 0;
    QDomDocument dom = SvgParser::createDocumentFromSvg(svg, &errorMsg, &errorLine, &errorColumn);
    if (dom.isNull())
        return false;

    KisShapeLayerSP layer(
        new KisShapeLayer(document->shapeController(), image, layerName, OPACITY_OPAQUE_U8));
    auto *container = dynamic_cast<KoShapeContainer *>(layer.data());
    if (!container)
        return false;

    QSizeF fragmentSize;
    SvgParser parser(document->shapeController()->resourceManager());
    parser.setResolution(image->bounds(), image->xRes() * 72.0);
    QList<KoShape *> newShapes = parser.parseSvg(dom.documentElement(), &fragmentSize);
    if (newShapes.isEmpty())
        return false;

    KUndo2Command *cmd =
        new KoShapeCreateCommand(document->shapeController(), newShapes, container);
    KisProcessingApplicator::runSingleCommandStroke(image, cmd);
    image->waitForDone();

    KisNodeSP parent = image->rootLayer();
    KisNodeSP above = insertAbove;
    if (above)
        parent = above->parent();
    if (!parent)
        parent = image->rootLayer();
    image->addNode(layer, parent, above);

    const QUuid id = layer->uuid();
    bool found = false;
    for (const TrackEntry &e : m_tracked) {
        if (e.layerUuid == id && e.image.toStrongRef() == image) {
            found = true;
            break;
        }
    }
    if (!found) {
        TrackEntry te;
        te.image = image;
        te.layerUuid = id;
        m_tracked.append(te);
    }
    if (!m_pollTimer.isActive())
        m_pollTimer.start();
    syncCachedSvg(layer.data(), image);
    return true;
}

void ComfyUIPoseLayers::slotPoll()
{
    if (m_tracked.isEmpty()) {
        m_pollTimer.stop();
        return;
    }
    for (int i = 0; i < m_tracked.size();) {
        TrackEntry &e = m_tracked[i];
        const KisImageSP img = e.image.toStrongRef();
        if (!img) {
            m_lastSvgByUuid.remove(e.layerUuid);
            m_tracked.removeAt(i);
            continue;
        }
        KisNodeSP node = KisLayerUtils::findNodeByUuid(img->root(), e.layerUuid);
        auto *sl = qobject_cast<KisShapeLayer *>(node.data());
        if (!sl) {
            ++i;
            continue;
        }
        syncCachedSvg(sl, img);
        ++i;
    }
}
