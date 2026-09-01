#include "src/qmlmodels/SettingsViewModel.h"
#include "src/platform/windows/appearance/WindowAppearance.h"

#include <QGuiApplication>
#include <QPointer>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QStyleHints>
#include <QTimer>
#include <QWindow>

#ifndef WAM_VERSION
#define WAM_VERSION "0.1.0"
#endif

namespace {

#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
void applyApplicationAppearance(QWindow *window, int themeMode)
{
    Qt::ColorScheme scheme = Qt::ColorScheme::Unknown;
    if (themeMode == 1)
        scheme = Qt::ColorScheme::Light;
    else if (themeMode == 2)
        scheme = Qt::ColorScheme::Dark;
    QGuiApplication::styleHints()->setColorScheme(scheme);

    const bool dark = themeMode == 2
            || (themeMode == 0
                && QGuiApplication::styleHints()->colorScheme()
                        == Qt::ColorScheme::Dark);
    wam::platform::windows::setDarkTitleBar(window, dark);
}
#endif

} // namespace

int main(int argc, char *argv[])
{
#if defined(Q_OS_WIN) && QT_VERSION_CHECK(5, 6, 0) <= QT_VERSION && QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
#endif

    QQuickStyle::setStyle(QStringLiteral("Basic"));

    QGuiApplication app(argc, argv);
    const bool startupCheck = qEnvironmentVariableIsSet("WAM_STARTUP_CHECK")
            || app.arguments().contains(QStringLiteral("--startup-check"));
    QCoreApplication::setOrganizationName(QStringLiteral("WindowsAppDataManager"));
    QCoreApplication::setApplicationName(QStringLiteral("WindowsAppDataManager"));
    QCoreApplication::setApplicationVersion(QString::fromLatin1(WAM_VERSION));

    QQmlApplicationEngine engine;
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/windowsappdatamanager/main.qml")));
    if (engine.rootObjects().isEmpty())
        return -1;

#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    QObject *rootObject = engine.rootObjects().constFirst();
    auto *window = qobject_cast<QWindow *>(rootObject);
    auto *settings = qobject_cast<wam::qmlmodels::SettingsViewModel *>(
            rootObject->property("settingsController").value<QObject *>());
    if (window && settings) {
        applyApplicationAppearance(window, settings->themeMode());
        const QPointer<QWindow> windowGuard(window);
        const QPointer<wam::qmlmodels::SettingsViewModel> settingsGuard(settings);
        QObject::connect(settings,
                         &wam::qmlmodels::SettingsViewModel::themeModeChanged,
                         window,
                         [windowGuard, settingsGuard] {
            if (windowGuard && settingsGuard) {
                applyApplicationAppearance(
                        windowGuard, settingsGuard->themeMode());
            }
        });
        QObject::connect(QGuiApplication::styleHints(),
                         &QStyleHints::colorSchemeChanged,
                         window,
                         [windowGuard, settingsGuard](Qt::ColorScheme) {
            if (windowGuard && settingsGuard
                    && settingsGuard->themeMode() == 0) {
                applyApplicationAppearance(windowGuard, 0);
            }
        });
    }
#endif

    if (startupCheck) {
        QTimer::singleShot(750, &app, &QCoreApplication::quit);
        return app.exec();
    }

    return app.exec();
}
