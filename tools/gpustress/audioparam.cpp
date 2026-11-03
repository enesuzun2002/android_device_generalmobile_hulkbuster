/* audioparam get <keys> | set <key=value;...>: AudioSystem parameters. */
#include <binder/ProcessState.h>
#include <media/AudioSystem.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char** argv) {
    if (argc < 3) { fprintf(stderr, "usage: audioparam get|set <string>\n"); return 2; }
    android::ProcessState::self()->startThreadPool();
    if (!strcmp(argv[1], "set")) {
        android::status_t s = android::AudioSystem::setParameters(android::String8(argv[2]));
        printf("set '%s' -> %d\n", argv[2], s);
    } else {
        android::String8 r = android::AudioSystem::getParameters(android::String8(argv[2]));
        printf("get '%s' -> '%s'\n", argv[2], r.string());
    }
    return 0;
}
