// Set the MT6592 DSI (MIPI) clock live through MTK's fbconfig debugfs node.
// usage: fbclk <MHz>   (same units as LCM dsi.PLL_CLOCK)
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>
#define MIPI_SET_CLK _IOW('X', 51, unsigned int)
int main(int argc, char **argv) {
    if (argc != 2) { fprintf(stderr, "usage: %s <MHz>\n", argv[0]); return 2; }
    unsigned int clk = (unsigned int)atoi(argv[1]);
    int fd = open("/sys/kernel/debug/fbconfig", O_RDWR);
    if (fd < 0) { perror("open /sys/kernel/debug/fbconfig"); return 1; }
    int r = ioctl(fd, MIPI_SET_CLK, &clk);
    if (r < 0) perror("ioctl MIPI_SET_CLK");
    close(fd);
    return r < 0;
}
