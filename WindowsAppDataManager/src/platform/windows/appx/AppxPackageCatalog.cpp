#include "AppxPackageCatalog.h"

#include <QDir>

#include <algorithm>
#include <exception>
#include <utility>

#ifdef Q_OS_WIN
#include <Windows.h>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Management.Deployment.h>
#include <winrt/Windows.Storage.h>
#include <winrt/base.h>
#endif

namespace wam::platform::windows {
namespace {

#ifdef Q_OS_WIN
QString fromHString(const winrt::hstring &value)
{
    return QString::fromWCharArray(value.c_str(), static_cast<qsizetype>(value.size()));
}

QString errorText(const winrt::hresult_error &error)
{
    return QStringLiteral("HRESULT 0x%1：%2")
            .arg(static_cast<quint32>(error.code().value), 8, 16, QLatin1Char('0'))
            .arg(fromHString(error.message()));
}

class ApartmentScope final {
public:
    ApartmentScope()
    {
        try {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
            m_uninitialize = true;
        } catch (const winrt::hresult_error &error) {
            if (error.code().value != RPC_E_CHANGED_MODE)
                throw;
        }
    }

    ~ApartmentScope()
    {
        if (m_uninitialize)
            winrt::uninit_apartment();
    }

    ApartmentScope(const ApartmentScope &) = delete;
    ApartmentScope &operator=(const ApartmentScope &) = delete;

private:
    bool m_uninitialize = false;
};
#endif

} // namespace

AppxPackageQueryResult AppxPackageCatalog::installedForCurrentUser()
{
    AppxPackageQueryResult result;
#ifdef Q_OS_WIN
    try {
        ApartmentScope apartment;
        const winrt::Windows::Management::Deployment::PackageManager manager;
        const auto packages = manager.FindPackagesForUser(L"");
        result.available = true;
        for (const winrt::Windows::ApplicationModel::Package &package : packages) {
            AppxPackageInfo info;
            try {
                const auto id = package.Id();
                info.name = fromHString(id.Name());
                info.familyName = fromHString(id.FamilyName());
                info.publisherId = fromHString(id.PublisherId());
                info.publisher = fromHString(id.Publisher());
            } catch (const winrt::hresult_error &error) {
                result.issues.append(QStringLiteral("已跳过一个无法读取身份的 AppX 包：%1")
                                             .arg(errorText(error)));
                continue;
            }

            try {
                info.displayName = fromHString(package.DisplayName());
            } catch (const winrt::hresult_error &error) {
                result.issues.append(QStringLiteral("无法读取包 %1 的显示名称：%2")
                                             .arg(info.familyName, errorText(error)));
            }
            try {
                info.publisherDisplayName = fromHString(package.PublisherDisplayName());
            } catch (const winrt::hresult_error &error) {
                result.issues.append(QStringLiteral("无法读取包 %1 的发布者显示名称：%2")
                                             .arg(info.familyName, errorText(error)));
            }
            try {
                info.framework = package.IsFramework();
                info.resourcePackage = package.IsResourcePackage();
            } catch (const winrt::hresult_error &error) {
                result.issues.append(QStringLiteral("无法读取包 %1 的类型信息：%2")
                                             .arg(info.familyName, errorText(error)));
            }
            try {
                info.installPath = QDir::toNativeSeparators(
                        fromHString(package.InstalledLocation().Path()));
            } catch (const winrt::hresult_error &error) {
                result.issues.append(QStringLiteral("无法读取包 %1 的安装位置：%2")
                                             .arg(info.familyName, errorText(error)));
            }
            result.packages.append(std::move(info));
        }
        std::sort(result.packages.begin(), result.packages.end(),
                  [](const AppxPackageInfo &left, const AppxPackageInfo &right) {
            return left.familyName.compare(right.familyName, Qt::CaseInsensitive) < 0;
        });
    } catch (const winrt::hresult_error &error) {
        result.issues.append(QStringLiteral("无法枚举当前用户的 AppX / MSIX 包：%1")
                                     .arg(errorText(error)));
    } catch (const std::exception &error) {
        result.issues.append(QStringLiteral("无法枚举当前用户的 AppX / MSIX 包：%1")
                                     .arg(QString::fromUtf8(error.what())));
    }
#else
    result.issues.append(QStringLiteral("当前平台不支持 AppX / MSIX 包枚举"));
#endif
    return result;
}

} // namespace wam::platform::windows
