#include <media/AudioSystem.h>
#include <utils/String8.h>
#include <stdio.h>

using namespace android;

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: %s <key=value>\n", argv[0]);
        return 1;
    }
    String8 str(argv[1]);
    status_t status = AudioSystem::setParameters(str);
    printf("setParameters('%s') returned %d\n", argv[1], status);
    return 0;
}

