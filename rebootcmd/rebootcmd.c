#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <cutils/properties.h>
#include <android/log.h>

#define ALOGD(...) __android_log_print(ANDROID_LOG_DEBUG, "rebootcmd: ", __VA_ARGS__)

int main(int argc, char**argv, char *envp[])
{

	ALOGD("Setting sys.reboot.reason = %s", argv[1]);

	//returns 1 if error
	ALOGD("return = %d", property_set("sys.reboot.reason", argv[1]));


	//Needed to allow the script to write to /boot/moboot.next
	sleep(1);
	return -1;
}
