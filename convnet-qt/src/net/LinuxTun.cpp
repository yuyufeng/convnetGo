#ifdef __linux__

#include "net/LinuxTun.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <net/if.h>
#include <linux/if_tun.h>
#include <cstring>
#include <cstdlib>
#include <cerrno>

LinuxTun::~LinuxTun() { close(); }

bool LinuxTun::open(QString& ifName, const QString& ip, int prefixLen, QString& err)
{
    m_fd = ::open("/dev/net/tun", O_RDWR);
    if (m_fd < 0) {
        err = QStringLiteral("打开 /dev/net/tun 失败（需要 root / CAP_NET_ADMIN）");
        return false;
    }

    struct ifreq ifr;
    std::memset(&ifr, 0, sizeof(ifr));
    ifr.ifr_flags = IFF_TUN | IFF_NO_PI; // L3、无 4 字节包信息前缀
    std::strncpy(ifr.ifr_name, "cvn0", IFNAMSIZ - 1);

    if (::ioctl(m_fd, TUNSETIFF, &ifr) < 0) {
        err = QStringLiteral("TUNSETIFF 失败（%1）").arg(std::strerror(errno));
        ::close(m_fd);
        m_fd = -1;
        return false;
    }
    m_ifName = QString::fromUtf8(ifr.ifr_name);
    ifName = m_ifName;

    // 配置地址并启用（用 ip 命令，简单可靠；需 root）
    const QString addCmd = QStringLiteral("ip addr add %1/%2 dev %3 2>/dev/null")
                               .arg(ip).arg(prefixLen).arg(m_ifName);
    const QString upCmd = QStringLiteral("ip link set dev %1 up").arg(m_ifName);
    ::system(addCmd.toUtf8().constData());
    if (::system(upCmd.toUtf8().constData()) != 0)
        err = QStringLiteral("配置网卡 IP/UP 失败（需 root）");

    return true;
}

int LinuxTun::readPacket(char* buf, int maxLen)
{
    if (m_fd < 0)
        return -1;
    // 用 select 带 200ms 超时，超时返回 0，便于读循环及时检查停止标志退出
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(m_fd, &rfds);
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 200000;
    const int s = ::select(m_fd + 1, &rfds, nullptr, nullptr, &tv);
    if (s == 0)
        return 0; // 超时无数据
    if (s < 0)
        return (errno == EINTR) ? 0 : -1;
    return static_cast<int>(::read(m_fd, buf, maxLen));
}

bool LinuxTun::writePacket(const char* data, int len)
{
    if (m_fd < 0)
        return false;
    return ::write(m_fd, data, len) == len;
}

void LinuxTun::close()
{
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
}

ITapDevice* createTapDevice() { return new LinuxTun(); }

#endif // __linux__
