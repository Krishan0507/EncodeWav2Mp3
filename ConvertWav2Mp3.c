#include <dirent.h>
#include <lame/lame.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>

#define WAV_BUFFER_SIZE 8192
#define MP3_BUFFER_SIZE 8192

/* Structure for thread parameters */
typedef struct MP3ConvertorThreadParams{
	char *wav_file_path;
	char *mp3_file_path;
	int *number_of_threads;
	pthread_mutex_t *mutex;
} MP3ConvertorThreadParams;

/* Routine to encode wav to mp3 */
static void * Encode2Mp3ThreadRoutine( void *params ){
	/* Cast the given parameter to MP3ConvertorThreadParams */
	struct MP3ConvertorThreadParams *MP3ConvertorThreadParamsPtr = ((MP3ConvertorThreadParams *) (params));

	/* Print start information */
	if( pthread_mutex_lock(MP3ConvertorThreadParamsPtr->mutex) == 0 ) {
		// printf( "\nEncoding: %ld \n", pthread_self() );

		printf( " %s from %s\n", MP3ConvertorThreadParamsPtr->mp3_file_path, MP3ConvertorThreadParamsPtr->wav_file_path );

		pthread_mutex_unlock( MP3ConvertorThreadParamsPtr->mutex );
	}

	/* Encode */
	if( Encode_Wav2Mp3(MP3ConvertorThreadParamsPtr->wav_file_path, MP3ConvertorThreadParamsPtr->mp3_file_path, MP3ConvertorThreadParamsPtr->mutex) != EXIT_SUCCESS ){
		/* Close thread with failure */
		pthread_exit( (void*) EXIT_FAILURE );
	}

	/* Lock mutex, decrement number of running threads, unlock */
	if( pthread_mutex_lock(MP3ConvertorThreadParamsPtr->mutex) == 0 ){
		if( *MP3ConvertorThreadParamsPtr->number_of_threads > 0 ){
			*MP3ConvertorThreadParamsPtr->number_of_threads -= 1;
		}

		pthread_mutex_unlock( MP3ConvertorThreadParamsPtr->mutex );
	}

	/* Print finish information */
	if( pthread_mutex_lock(MP3ConvertorThreadParamsPtr->mutex) == 0 ){
		// printf( "\nEND: %ld ", pthread_self() );
		printf( "%s encoded succesfully\n", MP3ConvertorThreadParamsPtr->wav_file_path );

		pthread_mutex_unlock( MP3ConvertorThreadParamsPtr->mutex );
	}

	/* Free Memory*/
	free( MP3ConvertorThreadParamsPtr->wav_file_path );
	free( MP3ConvertorThreadParamsPtr->mp3_file_path );
	free( MP3ConvertorThreadParamsPtr );

	/* Close thread with success */
	pthread_exit( (void*)EXIT_SUCCESS );
}

/* Encode a wave file to a mp3 file */
int Encode_Wav2Mp3( char *wav_file_path, char *mp3_file_path, pthread_mutex_t *mutex ){
	/* Encoder flags */
	lame_global_flags *lameflags;

	/* Source and destination file */
	FILE *wavfile, *mp3file;

	/* Holder for return values */
	size_t bytesread, byteswrote;

	/* Stream buffers */
	short *wav_buffer;
	unsigned char *mp3_buffer;

	/* Channel buffers */
	short *buffer_left;
	short *buffer_right;

	int i, j;

	/* initialize the lame encoder */
	lameflags = lame_init( );

	/* default options are 2, 44.1khz, 128kbps CBR, jstereo, quality 5 */
	lame_set_num_channels( lameflags, 2 );
	lame_set_in_samplerate( lameflags, 44100 );
	lame_set_brate( lameflags, 128 );
	lame_set_mode( lameflags, 1 );	/* 0,1,2,3 = stereo, jstereo, dual channel (not supported), mono */
	lame_set_quality( lameflags, 0 );	/* 0=best 5=good 9=worst */ 

	/* set internal parameters */
	if( lame_init_params(lameflags) < 0 ) {
		if( pthread_mutex_lock(mutex) == 0 ){
			pthread_mutex_unlock( mutex );
		}
	}

	/* Open files */
	if( (wavfile = fopen(wav_file_path, "rb")) == NULL ){
		if( pthread_mutex_lock(mutex) == 0 ){
			fprintf( stderr, "ERROR: cannot find path %s pls check again\n", wav_file_path );

			pthread_mutex_unlock( mutex );
		}

		/* Return with failure */
		return( EXIT_FAILURE );
	}

	if( (mp3file = fopen(mp3_file_path, "wb")) == NULL ){
		if( pthread_mutex_lock(mutex) == 0 ){
			fprintf( stderr, "ERROR: cannot find path %s pls check again\n", mp3_file_path );

			pthread_mutex_unlock( mutex );
		}

		/* return with failure */
		return( EXIT_FAILURE );
	}

	/* Initialize wave & mp3 buffers */
	wav_buffer = malloc( sizeof(short) * 2 * WAV_BUFFER_SIZE );
	mp3_buffer = malloc( sizeof(unsigned char) * MP3_BUFFER_SIZE );

	/* Read wave file, encode, write mp3 file */
	do{
		/* Read wave file */
		bytesread = fread( wav_buffer, 2 * sizeof(short), WAV_BUFFER_SIZE, wavfile);

		/* Bytes from wave file available */
		if( bytesread != 0 ){
			/* Initialize buffers for left and right channel */
			buffer_left = malloc( sizeof(short)*bytesread );
			buffer_right = malloc( sizeof(short)*bytesread );

			/* Copy / split streambuffer to channelbuffers */
			j=0;
			for( i = 0; i < bytesread; i++ ){
				buffer_left[i] = wav_buffer[j++];
				buffer_right[i] = wav_buffer[j++];
			}

			/* Encode channelbuffers to mp3 buffer */
			byteswrote = lame_encode_buffer( lameflags, buffer_left, buffer_right, bytesread, mp3_buffer, MP3_BUFFER_SIZE );

			/* Free channelbuffers */
			free( buffer_left );
			free( buffer_right );
		}else{ /* No (more) byte from wave file available */
			byteswrote = lame_encode_flush( lameflags, mp3_buffer, MP3_BUFFER_SIZE );
		}

		/* Check for encoding errors */
		if( byteswrote < 0 ){
			if( pthread_mutex_lock(mutex) == 0 ){
				fprintf( stderr, "ERROR during encoding, bytes wrote: %ld\n", byteswrote );

				pthread_mutex_unlock( mutex );
			}

			/* Return with failure */
			return( EXIT_FAILURE );
		}

		/* Write mp3 buffer to mp3 file */
		fwrite( mp3_buffer, byteswrote, 1, mp3file );

	} while( bytesread != 0 );

	/* Close lame flags */
	lame_close( lameflags );

	/* Free memory */
	free( wav_buffer );
	free( mp3_buffer );

	/* Close files */
	fclose( wavfile );
	fclose( mp3file );

	/* Return with success */
	return( EXIT_SUCCESS );
}

int main( int argc, char **argv ){

	// cpu_set_t cpuset;

    // CPU_ZERO(&cpuset);

	/* Source directory */
	DIR *src_directory;

	/* File entry in directory */
	struct dirent *entry;

	/* Path/file names */
	char *path, *wav_file_path, *mp3_file_path, *extension;

	/* Parameters for encoding thread(s) */
	MP3ConvertorThreadParams *parameters;

	/* Temporary threadt */
	pthread_t tmpThread;
	
	/* Mutex */
	pthread_mutex_t mutex;

	/* Number of cpu cores, running threads */
	long number_of_processors = 0;
	int number_of_threads = 0;

	/* Get commandline arguments */
	if( argc != 2 ){
		fprintf( stderr, "Usage: %s <path_to_folder_with_wav_files>\n", argv[0] );

		/* Exit with failure */
		return( EXIT_FAILURE );
	}

	path = malloc( strlen(argv[1]) + 2 );
	strcpy( path, argv[1] );

	number_of_processors = sysconf( _SC_NPROCESSORS_ONLN );

	/* Set affinity mask to include all CPUs */
	// int j = 0;
    // for (j = 0; j < number_of_processors; j++){
	// 	CPU_SET(j, &cpuset);
	// }        

	/* Open src_directory containing wave files */
	if( (src_directory = opendir(path)) == NULL ){
		fprintf( stderr, "ERROR opening src_directory: %s\n", path );

		/* Exit with failure */
		return( EXIT_FAILURE );
	}

	/* Initialize mutex */
	pthread_mutex_init( &mutex, NULL );

	/* Run through files in src_directory */
	while( (entry = readdir(src_directory)) != NULL ){

		/* If file has the '.wav' extension */
		if( strcmp(strrchr(entry->d_name, '.'), ".wav") == 0 ){

			/* Build filepaths */
			if( path[strlen(path)-1] != '/' ){
				strcat( path, "/" );
			}

			wav_file_path = malloc( sizeof(char) * (strlen(path) + strlen(entry->d_name) + 1) );

			strcpy( wav_file_path, path );
			strcat( wav_file_path, entry->d_name );

			mp3_file_path = malloc( sizeof(char) * (strlen(wav_file_path) + 1) );

			strcpy( mp3_file_path, wav_file_path );
			extension = strrchr( mp3_file_path, '.' );
			strcpy( extension, ".mp3" );
			
			/* build parameters structure for encoding-thread */
			parameters = malloc( sizeof(struct MP3ConvertorThreadParams) );
			parameters->wav_file_path = malloc( strlen(wav_file_path) + 1 );

			strcpy( parameters->wav_file_path, wav_file_path );
			parameters->mp3_file_path = malloc( strlen(mp3_file_path) + 1 );

			strcpy( parameters->mp3_file_path, mp3_file_path );

			parameters->number_of_threads = &number_of_threads;
			parameters->mutex = &mutex;

			/* Encode wave file to mp3 file */
			if( pthread_create(&tmpThread, NULL, Encode2Mp3ThreadRoutine, parameters) != 0 ){
				pthread_cancel( tmpThread );
			}else{
			
				/* lock mutex, increment number of running threads, unlock */
				if( pthread_mutex_lock(&mutex) == 0 ){
					number_of_threads += 1;

					pthread_mutex_unlock( &mutex );
				}

				/* Disassociate encoder thread from main */
				pthread_detach(tmpThread);
			}
		}
	}

	/* Wait for running theads to finish */
	while( number_of_threads > 0 ){
		sleep( 1 );
	}

	printf( "\nAll threads finished, closing...\n" );

	/* free memory */
	free( path );

	/* destroy mutex */
	pthread_mutex_destroy( &mutex );

	/* exit with success */
	return( EXIT_SUCCESS );
}
