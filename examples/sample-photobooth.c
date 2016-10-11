/* compile with gcc -Wall -o sample-photobooth -lgphoto2 sample-photobooth.c
 * 
 */

#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <signal.h>
#include <gphoto2/gphoto2.h>

#include "samples.h"

static void errordumper(GPLogLevel level, const char *domain, const char *str,
                 void *data) {
/*	printf("%s (data %p)\n", str,data);*/
}

/* set by signal handler */
static int capture_now = 0;
static int read_config = 0;

static int
capture_to_file(Camera *camera, GPContext *context, char *fn) {
	int fd, retval;
	CameraFile *file;
	CameraFilePath camera_file_path;

	retval = gp_camera_capture(camera, GP_CAPTURE_IMAGE, &camera_file_path, context);
	if (retval < GP_OK) return retval;

	fd = open(fn, O_CREAT | O_WRONLY, 0644);
	retval = gp_file_new_from_fd(&file, fd);
	if (retval < GP_OK) {
		close(fd);
		unlink(fn);
		return retval;
	}

	retval = gp_camera_file_get(camera, camera_file_path.folder, camera_file_path.name,
		     GP_FILE_TYPE_NORMAL, file, context);
	if (retval < GP_OK) {
		gp_file_free(file);
		unlink(fn);
		return retval;
	}

	retval = gp_camera_file_delete(camera, camera_file_path.folder, camera_file_path.name, context);
	gp_file_free(file);
	return GP_OK;
}

static void
sig_handler_capture_now (int sig_num)
{
        signal (SIGUSR1, sig_handler_capture_now);
        capture_now = 1;
}

static void
sig_handler_read_config (int sig_num)
{
        signal (SIGUSR2, sig_handler_read_config);
        read_config = 1;
}

int
main(int argc, char **argv) {
	Camera	*camera;
	int	retval;
	int	capturecnt = 0;
	GPContext *context = sample_create_context();

	printf("Sample photobooth.\n");
	printf("Continously stores previews in 'preview.jpg'.\n");
	printf("kill -USR1 %d to take a capture.\n", getpid());
	printf("kill -USR2 %d to read the 'config.txt'.\n", getpid());
	printf("kill -TERM %d to finish.\n", getpid());

        signal (SIGUSR1, sig_handler_capture_now);
        signal (SIGUSR2, sig_handler_read_config);

	gp_log_add_func(GP_LOG_ERROR, errordumper, 0);
	gp_camera_new(&camera);

	retval = gp_camera_init(camera, context);
	if (retval != GP_OK) {
		printf("  Retval: %d\n", retval);
		exit (1);
	}

	while (1) {
		CameraFile *file;
		char output_file[32];

		/*
		 * Capture a full picture on demand. Use unique filenames.
		 */
		if (capture_now) {
			capture_now = 0;
			sprintf(output_file, "image-%03d.jpg", capturecnt++);
			capture_to_file(camera, context, output_file);
		}

		/*
		 * Capture a preview on every loop. Save as preview.jpg.
		 */
		retval = gp_file_new(&file);
		if (retval != GP_OK) {
			fprintf(stderr,"gp_file_new: %d\n", retval);
			exit(1);
		}

		retval = gp_camera_capture_preview(camera, file, context);
		if (retval != GP_OK) {
			fprintf(stderr,"gp_camera_capture_preview failed: %d\n", retval);
			exit(1);
		}
		retval = gp_file_save(file, "preview.jpg");
		if (retval != GP_OK) {
			fprintf(stderr,"saving preview failed: %d\n", retval);
			exit(1);
		}
		gp_file_unref(file);
		/*
		 * Check and drain the event queue.
		 * 	Download newly captured images.
		 *	Ignore the rest events.
		 */
		while (1) {
			CameraEventType	evttype;
			CameraFilePath	*path;
			void    	*evtdata;
			int		fd;

			evtdata = NULL;
			retval = gp_camera_wait_for_event (camera, 1, &evttype, &evtdata, context);
			if (retval != GP_OK) break;
			switch (evttype) {
			case GP_EVENT_FILE_ADDED:
				path = (CameraFilePath*)evtdata;
				printf("File added on the camera: %s/%s\n", path->folder, path->name);

				sprintf(output_file, "image-%03d.jpg", capturecnt++);

				fd = open(output_file, O_CREAT | O_WRONLY, 0644);
				retval = gp_file_new_from_fd(&file, fd);
				retval = gp_camera_file_get(camera, path->folder, path->name,
					     GP_FILE_TYPE_NORMAL, file, context);

				retval = gp_camera_file_delete(camera, path->folder, path->name, context);
				gp_file_free(file);
				free (evtdata);
				break;
			case GP_EVENT_FOLDER_ADDED:
				path = (CameraFilePath*)evtdata;
				printf("Folder added on camera: %s / %s\n", path->folder, path->name);
				free (evtdata);
				break;
			case GP_EVENT_CAPTURE_COMPLETE:
				printf("Capture Complete.\n");
				break;
			case GP_EVENT_TIMEOUT:
				break;
			case GP_EVENT_UNKNOWN:
				if (evtdata) {
					printf("Unknown event: %s.\n", (char*)evtdata);
					free (evtdata);
				} else {
					printf("Unknown event.\n");
				}
				break;
			default:
				printf("Type %d?\n", evttype);
				break;
			}
			if (evttype == GP_EVENT_TIMEOUT)
				break;
		}
	}
	gp_camera_exit(camera, context);
	return 0;
}