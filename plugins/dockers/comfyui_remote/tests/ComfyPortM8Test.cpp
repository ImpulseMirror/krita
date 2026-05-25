/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <simpletest.h>
#include <QJsonObject>

#include "ComfyUIUtils.h"

class ComfyPortM8Test : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testValidateCustomWorkflowApiGraphEmpty();
    void testValidateCustomWorkflowApiGraphMissingClassType();
    void testValidateCustomWorkflowApiGraphMissingServerNode();
    void testValidateCustomWorkflowApiGraphOk();
    void testValidateCustomWorkflowRequiresOutputNode();
    void testValidateCustomWorkflowApiGraphRejectsNoOutput();
};

void ComfyPortM8Test::testValidateCustomWorkflowApiGraphEmpty()
{
    const auto r = ComfyUIUtils::validateCustomWorkflowApiGraph(QJsonObject());
    QVERIFY(!r.first);
    QVERIFY(!r.second.isEmpty());
}

void ComfyPortM8Test::testValidateCustomWorkflowApiGraphMissingClassType()
{
    QJsonObject wf;
    QJsonObject n1;
    n1.insert(QStringLiteral("inputs"), QJsonObject{{QStringLiteral("seed"), 1}});
    wf.insert(QStringLiteral("1"), n1);
    const auto r = ComfyUIUtils::validateCustomWorkflowApiGraph(wf);
    QVERIFY(!r.first);
}

void ComfyPortM8Test::testValidateCustomWorkflowApiGraphMissingServerNode()
{
    QJsonObject wf;
    wf.insert(QStringLiteral("1"),
              QJsonObject{{QStringLiteral("class_type"), QStringLiteral("UnknownNodeXYZ")},
                          {QStringLiteral("inputs"), QJsonObject()}});
    QJsonObject objectInfo;
    objectInfo.insert(QStringLiteral("KSampler"), QJsonObject());
    const auto r = ComfyUIUtils::validateCustomWorkflowApiGraph(wf, objectInfo);
    QVERIFY(!r.first);
    QVERIFY(r.second.contains(QStringLiteral("UnknownNodeXYZ")));
}

void ComfyPortM8Test::testValidateCustomWorkflowApiGraphOk()
{
    QJsonObject wf;
    wf.insert(QStringLiteral("3"),
              QJsonObject{{QStringLiteral("class_type"), QStringLiteral("KSampler")},
                          {QStringLiteral("inputs"), QJsonObject{{QStringLiteral("seed"), 1}}}});
    wf.insert(QStringLiteral("9"),
              QJsonObject{{QStringLiteral("class_type"), QStringLiteral("SaveImage")},
                          {QStringLiteral("inputs"), QJsonObject{{QStringLiteral("filename_prefix"), QStringLiteral("out")}}}});
    QJsonObject objectInfo;
    objectInfo.insert(QStringLiteral("KSampler"), QJsonObject());
    objectInfo.insert(QStringLiteral("SaveImage"), QJsonObject());
    const auto r = ComfyUIUtils::validateCustomWorkflowApiGraph(wf, objectInfo);
    QVERIFY(r.first);
}

void ComfyPortM8Test::testValidateCustomWorkflowRequiresOutputNode()
{
    QJsonObject wf;
    wf.insert(QStringLiteral("1"),
              QJsonObject{{QStringLiteral("class_type"), QStringLiteral("KSampler")},
                          {QStringLiteral("inputs"), QJsonObject()}});
    QVERIFY(!ComfyUIUtils::validateCustomWorkflowHasOutputNode(wf));
    wf.insert(QStringLiteral("2"),
              QJsonObject{{QStringLiteral("class_type"), QStringLiteral("ETN_ReturnImage")},
                          {QStringLiteral("inputs"), QJsonObject()}});
    QVERIFY(ComfyUIUtils::validateCustomWorkflowHasOutputNode(wf));
}

void ComfyPortM8Test::testValidateCustomWorkflowApiGraphRejectsNoOutput()
{
    QJsonObject wf;
    wf.insert(QStringLiteral("1"),
              QJsonObject{{QStringLiteral("class_type"), QStringLiteral("KSampler")},
                          {QStringLiteral("inputs"), QJsonObject()}});
    const auto r = ComfyUIUtils::validateCustomWorkflowApiGraph(wf);
    QVERIFY(!r.first);
}

SIMPLE_TEST_MAIN(ComfyPortM8Test)
#include "ComfyPortM8Test.moc"
