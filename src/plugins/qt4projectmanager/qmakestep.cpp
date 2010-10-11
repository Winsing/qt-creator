/**************************************************************************
**
** This file is part of Qt Creator
**
** Copyright (c) 2010 Nokia Corporation and/or its subsidiary(-ies).
**
** Contact: Nokia Corporation (qt-info@nokia.com)
**
** Commercial Usage
**
** Licensees holding valid Qt Commercial licenses may use this file in
** accordance with the Qt Commercial License Agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Nokia.
**
** GNU Lesser General Public License Usage
**
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** If you are unsure which license is appropriate for your use, please
** contact the sales department at http://qt.nokia.com/contact.
**
**************************************************************************/

#include "qmakestep.h"

#include "projectexplorer/projectexplorerconstants.h"
#include "qmakeparser.h"
#include "qt4buildconfiguration.h"
#include "qt4project.h"
#include "qt4projectmanagerconstants.h"
#include "qt4projectmanager.h"
#include "qt4target.h"
#include "qtversionmanager.h"

#include <projectexplorer/buildsteplist.h>

#include <coreplugin/icore.h>
#include <utils/qtcassert.h>

#include <QtCore/QDir>
#include <QtCore/QFile>

using namespace Qt4ProjectManager;
using namespace Qt4ProjectManager::Internal;
using namespace ProjectExplorer;

namespace {
const char * const QMAKE_BS_ID("QtProjectManager.QMakeBuildStep");

const char * const QMAKE_ARGUMENTS_KEY("QtProjectManager.QMakeBuildStep.QMakeArguments");
}

QMakeStep::QMakeStep(BuildStepList *bsl) :
    AbstractProcessStep(bsl, QLatin1String(QMAKE_BS_ID)),
    m_forced(false)
{
    ctor();
}

QMakeStep::QMakeStep(BuildStepList *bsl, const QString &id) :
    AbstractProcessStep(bsl, id),
    m_forced(false)
{
    ctor();
}

QMakeStep::QMakeStep(BuildStepList *bsl, QMakeStep *bs) :
    AbstractProcessStep(bsl, bs),
    m_forced(false),
    m_userArgs(bs->m_userArgs)
{
    ctor();
}

void QMakeStep::ctor()
{
    //: QMakeStep default display name
    setDefaultDisplayName(tr("qmake"));
}

QMakeStep::~QMakeStep()
{
}

Qt4BuildConfiguration *QMakeStep::qt4BuildConfiguration() const
{
    return static_cast<Qt4BuildConfiguration *>(buildConfiguration());
}

///
/// Returns all arguments
/// That is: possbile subpath
/// spec
/// config arguemnts
/// moreArguments
/// user arguments
QStringList QMakeStep::allArguments()
{
    QStringList additonalArguments = m_userArgs;
    Qt4BuildConfiguration *bc = qt4BuildConfiguration();
    QStringList arguments;
    if (bc->subNodeBuild())
        arguments << QDir::toNativeSeparators(bc->subNodeBuild()->path());
    else
        arguments << QDir::toNativeSeparators(buildConfiguration()->target()->project()->file()->fileName());
    arguments << "-r";

    if (!additonalArguments.contains("-spec"))
        arguments << "-spec" << bc->qtVersion()->mkspec();

    // Find out what flags we pass on to qmake
    arguments << bc->configCommandLineArguments();

    if (!additonalArguments.isEmpty())
        arguments << additonalArguments;

    arguments << moreArguments();

    return arguments;
}

///
/// moreArguments,
/// -unix for Maemo
/// -after OBJECTS_DIR, MOC_DIR, UI_DIR, RCC_DIR
/// QMAKE_VAR_QMLJSDEBUGGER_PATH
QStringList QMakeStep::moreArguments()
{
    Qt4BuildConfiguration *bc = qt4BuildConfiguration();
    QStringList arguments;
#if defined(Q_OS_WIN) || defined(Q_OS_MAC)
    ToolChain::ToolChainType type = bc->toolChainType();
    if (type == ToolChain::GCC_MAEMO)
        arguments << QLatin1String("-unix");
#endif
    if (bc->target()->id() == Constants::S60_DEVICE_TARGET_ID
        || bc->target()->id() == Constants::S60_EMULATOR_TARGET_ID) {
        // We have a target which does not allow shadow building.
        // But we really don't want to have the build artefacts in the source dir
        // so we try to hack around it, to make the common cases work.
        // This is a HACK, remove once the symbian make generator supports
        // shadow building
        arguments << QLatin1String("-after")
                  << QLatin1String("OBJECTS_DIR=obj")
                  << QLatin1String("MOC_DIR=moc")
                  << QLatin1String("UI_DIR=ui")
                  << QLatin1String("RCC_DIR=rcc");
    }

    // Do not turn debugger path into native path separators: Qmake does not like that!
    arguments << QLatin1String(Constants::QMAKEVAR_QMLJSDEBUGGER_PATH) + QLatin1Char('=') +
            Core::ICore::instance()->resourcePath() + QLatin1String("/qml/qmljsdebugger");
    return arguments;
}

bool QMakeStep::init()
{
    Qt4BuildConfiguration *qt4bc = qt4BuildConfiguration();
    const QtVersion *qtVersion = qt4bc->qtVersion();

    QStringList args = allArguments();
    QString workingDirectory;

    if (qt4bc->subNodeBuild())
        workingDirectory = qt4bc->subNodeBuild()->buildDir();
    else
        workingDirectory = qt4bc->buildDirectory();

    QString program = qtVersion->qmakeCommand();

    // Check whether we need to run qmake
    m_needToRunQMake = true;
    QString makefile = workingDirectory;

    if (qt4bc->subNodeBuild()) {
        if (!qt4bc->subNodeBuild()->makefile().isEmpty()) {
            makefile.append(qt4bc->subNodeBuild()->makefile());
        } else {
            makefile.append("/Makefile");
        }
    } else if (!qt4bc->makefile().isEmpty()) {
        makefile.append(qt4bc->makefile());
    } else {
        makefile.append("/Makefile");
    }

    if (QFileInfo(makefile).exists()) {
        QString qmakePath = QtVersionManager::findQMakeBinaryFromMakefile(makefile);
        if (qtVersion->qmakeCommand() == qmakePath) {
            m_needToRunQMake = !qt4bc->compareToImportFrom(makefile);
        }
    }

    if (m_forced) {
        m_forced = false;
        m_needToRunQMake = true;
    }

    setEnabled(m_needToRunQMake);
    setWorkingDirectory(workingDirectory);
    setCommand(program);
    setArguments(args);
    setEnvironment(qt4bc->environment());

    setOutputParser(new QMakeParser);

    Qt4ProFileNode *node = qt4bc->qt4Target()->qt4Project()->rootProjectNode();
    if (qt4bc->subNodeBuild())
        node = qt4bc->subNodeBuild();
    QString proFile = node->path();

    m_tasks = qt4BuildConfiguration()->qtVersion()->reportIssues(proFile, workingDirectory);
    m_scriptTemplate = node->projectType() == ScriptTemplate;

    return AbstractProcessStep::init();
}

void QMakeStep::run(QFutureInterface<bool> &fi)
{
    if (m_scriptTemplate) {
        fi.reportResult(true);
        return;
    }

    // Warn on common error conditions:

    bool canContinue = true;
    foreach (const ProjectExplorer::Task &t, m_tasks) {
        addTask(t);
        if (t.type == Task::Error)
            canContinue = false;
    }
    if (!canContinue) {
        emit addOutput(tr("Configuration is faulty, please check the Build Issues view for details."), BuildStep::MessageOutput);
        fi.reportResult(false);
        return;
    }

    if (!m_needToRunQMake) {
        emit addOutput(tr("Configuration unchanged, skipping qmake step."), BuildStep::MessageOutput);
        fi.reportResult(true);
        return;
    }

    AbstractProcessStep::run(fi);
}

void QMakeStep::setForced(bool b)
{
    m_forced = b;
}

bool QMakeStep::forced()
{
    return m_forced;
}

ProjectExplorer::BuildStepConfigWidget *QMakeStep::createConfigWidget()
{
    return new QMakeStepConfigWidget(this);
}

bool QMakeStep::immutable() const
{
    return false;
}

void QMakeStep::processStartupFailed()
{
    m_forced = true;
    AbstractProcessStep::processStartupFailed();
}

bool QMakeStep::processSucceeded(int exitCode, QProcess::ExitStatus status)
{
    bool result = AbstractProcessStep::processSucceeded(exitCode, status);
    if (!result)
        m_forced = true;
    qt4BuildConfiguration()->emitBuildDirectoryInitialized();
    return result;
}

void QMakeStep::setUserArguments(const QStringList &arguments)
{
    if (m_userArgs == arguments)
        return;
    m_userArgs = arguments;

    emit userArgumentsChanged();

    qt4BuildConfiguration()->emitQMakeBuildConfigurationChanged();
    qt4BuildConfiguration()->emitProFileEvaluteNeeded();
}

QStringList QMakeStep::parserArguments()
{
    QStringList result;
    foreach (const QString &str, allArguments()) {
        if (str.contains("="))
            result << str;
    }
    return result;
}

QStringList QMakeStep::userArguments()
{
    return m_userArgs;
}

QVariantMap QMakeStep::toMap() const
{
    QVariantMap map(AbstractProcessStep::toMap());
    map.insert(QLatin1String(QMAKE_ARGUMENTS_KEY), m_userArgs);
    return map;
}

bool QMakeStep::fromMap(const QVariantMap &map)
{
    m_userArgs = map.value(QLatin1String(QMAKE_ARGUMENTS_KEY)).toStringList();

    return BuildStep::fromMap(map);
}

////
// QMakeStepConfigWidget
////

QMakeStepConfigWidget::QMakeStepConfigWidget(QMakeStep *step)
    : BuildStepConfigWidget(), m_step(step), m_ignoreChange(false)
{
    m_ui.setupUi(this);
    connect(m_ui.qmakeAdditonalArgumentsLineEdit, SIGNAL(textEdited(const QString&)),
            this, SLOT(qmakeArgumentsLineEdited()));
    connect(m_ui.buildConfigurationComboBox, SIGNAL(currentIndexChanged(int)),
            this, SLOT(buildConfigurationSelected()));
    connect(step, SIGNAL(userArgumentsChanged()),
            this, SLOT(userArgumentsChanged()));
    connect(step->qt4BuildConfiguration(), SIGNAL(qtVersionChanged()),
            this, SLOT(qtVersionChanged()));
    connect(step->qt4BuildConfiguration(), SIGNAL(qmakeBuildConfigurationChanged()),
            this, SLOT(qmakeBuildConfigChanged()));
}

void QMakeStepConfigWidget::init()
{
    QString qmakeArgs = Utils::Environment::joinArgumentList(m_step->userArguments());
    m_ui.qmakeAdditonalArgumentsLineEdit->setText(qmakeArgs);

    qmakeBuildConfigChanged();

    updateSummaryLabel();
    updateEffectiveQMakeCall();
}

QString QMakeStepConfigWidget::summaryText() const
{
    return m_summaryText;
}

QString QMakeStepConfigWidget::displayName() const
{
    return m_step->displayName();
}

void QMakeStepConfigWidget::qtVersionChanged()
{
    updateSummaryLabel();
    updateEffectiveQMakeCall();
}

void QMakeStepConfigWidget::qmakeBuildConfigChanged()
{
    Qt4BuildConfiguration *bc = m_step->qt4BuildConfiguration();
    bool debug = bc->qmakeBuildConfiguration() & QtVersion::DebugBuild;
    int index = debug ? 0 : 1;
    if (bc->qmakeBuildConfiguration() & QtVersion::BuildAll)
        index = 2;
    m_ignoreChange = true;
    m_ui.buildConfigurationComboBox->setCurrentIndex(index);

    m_ignoreChange = false;
    updateSummaryLabel();
    updateEffectiveQMakeCall();
}

void QMakeStepConfigWidget::userArgumentsChanged()
{
    if (m_ignoreChange)
        return;
    QString qmakeArgs = Utils::Environment::joinArgumentList(m_step->userArguments());
    m_ui.qmakeAdditonalArgumentsLineEdit->setText(qmakeArgs);
    updateSummaryLabel();
    updateEffectiveQMakeCall();
}

void QMakeStepConfigWidget::qmakeArgumentsLineEdited()
{
    m_ignoreChange = true;
    m_step->setUserArguments(
            Utils::Environment::parseCombinedArgString(m_ui.qmakeAdditonalArgumentsLineEdit->text()));
    m_ignoreChange = false;

    updateSummaryLabel();
    updateEffectiveQMakeCall();
}

void QMakeStepConfigWidget::buildConfigurationSelected()
{
    if (m_ignoreChange)
        return;
    Qt4BuildConfiguration *bc = m_step->qt4BuildConfiguration();
    QtVersion::QmakeBuildConfigs buildConfiguration = bc->qmakeBuildConfiguration();
    switch (m_ui.buildConfigurationComboBox->currentIndex()) {
    case 0:
        buildConfiguration = QtVersion::DebugBuild;
        break;
    case 1:
        buildConfiguration = 0;
        break;
    case 2:
        buildConfiguration = QtVersion::BuildAll;
        break;
    }

    m_ignoreChange = true;
    bc->setQMakeBuildConfiguration(buildConfiguration);
    m_ignoreChange = false;

    updateSummaryLabel();
    updateEffectiveQMakeCall();
}

void QMakeStepConfigWidget::updateSummaryLabel()
{
    Qt4BuildConfiguration *qt4bc = m_step->qt4BuildConfiguration();
    const QtVersion *qtVersion = qt4bc->qtVersion();
    if (!qtVersion) {
        m_summaryText = tr("<b>qmake:</b> No Qt version set. Cannot run qmake.");
        emit updateSummary();
        return;
    }

    QStringList args = m_step->allArguments();
    // We don't want the full path to the .pro file
    const QString projectFileName = m_step->buildConfiguration()->target()->project()->file()->fileName();
    int index = args.indexOf(projectFileName);
    if (index != -1)
        args[index] = QFileInfo(projectFileName).fileName();

    // And we only use the .pro filename not the full path
    QString program = QFileInfo(qtVersion->qmakeCommand()).fileName();
    m_summaryText = tr("<b>qmake:</b> %1 %2").arg(program, args.join(QString(QLatin1Char(' '))));
    emit updateSummary();

}

void QMakeStepConfigWidget::updateEffectiveQMakeCall()
{
    Qt4BuildConfiguration *qt4bc = m_step->qt4BuildConfiguration();
    const QtVersion *qtVersion = qt4bc->qtVersion();
    QString program = QFileInfo(qtVersion->qmakeCommand()).fileName();
    m_ui.qmakeArgumentsEdit->setPlainText(program + QLatin1Char(' ') + Utils::Environment::joinArgumentList(m_step->allArguments()));
}

////
// QMakeStepFactory
////

QMakeStepFactory::QMakeStepFactory(QObject *parent) :
    ProjectExplorer::IBuildStepFactory(parent)
{
}

QMakeStepFactory::~QMakeStepFactory()
{
}

bool QMakeStepFactory::canCreate(BuildStepList *parent, const QString &id) const
{
    if (parent->id() != QLatin1String(ProjectExplorer::Constants::BUILDSTEPS_BUILD))
        return false;
    if (!qobject_cast<Qt4BuildConfiguration *>(parent->parent()))
        return false;
    return (id == QLatin1String(QMAKE_BS_ID));
}

ProjectExplorer::BuildStep *QMakeStepFactory::create(BuildStepList *parent, const QString &id)
{
    if (!canCreate(parent, id))
        return 0;
    return new QMakeStep(parent);
}

bool QMakeStepFactory::canClone(BuildStepList *parent, BuildStep *source) const
{
    return canCreate(parent, source->id());
}

ProjectExplorer::BuildStep *QMakeStepFactory::clone(BuildStepList *parent, ProjectExplorer::BuildStep *source)
{
    if (!canClone(parent, source))
        return 0;
    return new QMakeStep(parent, qobject_cast<QMakeStep *>(source));
}

bool QMakeStepFactory::canRestore(BuildStepList *parent, const QVariantMap &map) const
{
    QString id(ProjectExplorer::idFromMap(map));
    return canCreate(parent, id);
}

ProjectExplorer::BuildStep *QMakeStepFactory::restore(BuildStepList *parent, const QVariantMap &map)
{
    if (!canRestore(parent, map))
        return 0;
    QMakeStep *bs = new QMakeStep(parent);
    if (bs->fromMap(map))
        return bs;
    delete bs;
    return 0;
}

QStringList QMakeStepFactory::availableCreationIds(ProjectExplorer::BuildStepList *parent) const
{
    if (parent->id() == QLatin1String(ProjectExplorer::Constants::BUILDSTEPS_BUILD))
        if (Qt4BuildConfiguration *bc = qobject_cast<Qt4BuildConfiguration *>(parent->parent()))
            if (!bc->qmakeStep())
                return QStringList() << QLatin1String(QMAKE_BS_ID);
    return QStringList();
}

QString QMakeStepFactory::displayNameForId(const QString &id) const
{
    if (id == QLatin1String(QMAKE_BS_ID))
        return tr("qmake");
    return QString();
}
