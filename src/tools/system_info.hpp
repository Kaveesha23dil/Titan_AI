#ifndef TITANAI_SYSTEM_INFO_HPP
#define TITANAI_SYSTEM_INFO_HPP

#include <QString>
#include <QtGlobal>

struct SystemInfo {
    QString operatingSystem;
    QString kernelVersion;
    QString hostname;
    QString cpuModel;
    int cpuCoreCount{0};
    quint64 totalMemoryBytes{0};
    quint64 availableMemoryBytes{0};
    QString architecture;
};

class SystemInfoTool {
public:
    SystemInfoTool() = default;
    ~SystemInfoTool() = default;

    [[nodiscard]] SystemInfo getSystemInfo() const;
    [[nodiscard]] QString getOperatingSystem() const;
    [[nodiscard]] QString getKernelVersion() const;
    [[nodiscard]] QString getHostname() const;
    [[nodiscard]] QString getCpuModel() const;
    [[nodiscard]] int getCpuCoreCount() const;
    [[nodiscard]] quint64 getTotalMemoryBytes() const;
    [[nodiscard]] quint64 getAvailableMemoryBytes() const;
    [[nodiscard]] QString getArchitecture() const;
};

#endif // TITANAI_SYSTEM_INFO_HPP
