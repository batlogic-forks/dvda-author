/*

File:    audio.c
Purpose: Deal with reading the input audio files.

dvda-author  - Author a DVD-Audio DVD

Copyright Dave Chapman <dave@dchapman.com> 2005 ;
2008 revisions:
Copyright Fabrice Nicol <fabnicol@users.sourceforge.net> 2008 (SoX and FLAC 1.2.1 integration, code refactoring);
          Lee and Tim Feldkamp 2008 (multichannel features);

The latest version can be found at http://dvd-audio.sourceforge.net

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "structures.h"
#include "export.h"
#include "audio2.h"
#include "stream_decoder.h"
#include "c_utils.h"
#ifndef WITHOUT_FIXWAV
#include "fixwav.h"
#include "fixwav_manager.h"
#endif
#include "auxiliary.h"
#include "commonvars.h"
#include "command_line_parsing.h"
#include "winport.h"
#ifndef WITHOUT_sox
#include "sox.h"
#endif
#include "multichannel.h"


extern globalData globals;

/*
Multichannel reference tables are structured as follows:
  { { 16-bit  { channels { permutated values }}}, { 24-bit  { channels { permutated values }}} }

S is the table of direct conversion (WAV to AOB) and _S the table of reverse conversion (AOB to WAV), with _S o S = Id
*/

/* with static array execution time will be comparable to explicit hard-code value assignment */

static uint8_t  S[2][6][36]=
{{      {0}, {0},
        {5, 4, 11, 10, 1, 0, 3, 2, 7, 6, 9, 8},
        {5, 4, 7, 6, 13, 12, 15, 14, 1,  0, 3, 2, 9, 8, 11, 10},
        {5, 4, 7, 6,  9,  8, 15, 14,17, 16, 19, 18, 1, 0, 3, 2, 11, 10, 13, 12},
        {5, 4, 7, 6, 17, 16, 19, 18, 1, 0, 3, 2, 9, 8, 11, 10, 13, 12, 15, 14, 21, 20, 23, 22}
    },
    {      {2,  1,  5,  4,  0,  3},
        {2, 1, 5, 4, 8, 7, 11, 10, 0, 3, 6, 9},
        {8, 7, 17, 16, 6, 15, 2, 1, 5, 4, 11, 10, 14, 13, 0, 3, 9, 12},
        {8,  7,  11,  10,  20,  19,  23,  22,  6,  9,  18,  21,  2,  1,  5,  4,  14,  13,  17,  16,  0,  3,  12,  15},
        {8, 7, 11, 10, 14, 13, 23, 22, 26, 25, 29, 28, 6, 9, 12, 21, 24, 27, 2, 1, 5, 4, 17, 16, 20, 19, 0, 3, 15, 18},
        {8, 7, 11, 10, 26, 25, 29, 28, 6, 9, 24, 27, 2, 1, 5, 4, 14, 13, 17, 16, 20, 19, 23, 22, 32, 31, 35, 34, 0, 3, 12, 15, 18, 21, 30, 33 }
    }
};


#ifndef WITHOUT_FLAC

void flac_metadata_callback(const FLAC__StreamDecoder *dec, const FLAC__StreamMetadata *meta, void *data)
{
    fileinfo_t *info = (fileinfo_t *) data;

    if (meta->type==FLAC__METADATA_TYPE_STREAMINFO)
    {
        info->bitspersample=meta->data.stream_info.bits_per_sample;
        info->samplerate=meta->data.stream_info.sample_rate;
        info->channels=meta->data.stream_info.channels;
        info->numsamples=meta->data.stream_info.total_samples;
    }
}

FLAC__StreamDecoderWriteStatus flac_null_write_callback(const FLAC__StreamDecoder *dec,
        const FLAC__Frame *frame,
        const FLAC__int32 * const buf[],
        void *data)
{
    return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

FLAC__StreamDecoderWriteStatus flac_write_callback(const FLAC__StreamDecoder *dec,
        const FLAC__Frame *frame,
        const FLAC__int32 * const buf[],
        void *data)
{
    fileinfo_t *info = (fileinfo_t*) data;

    unsigned int c_samp, c_chan, d_samp;
    uint32_t data_size = frame->header.blocksize * frame->header.channels * (info->bitspersample / 8);
    uint32_t samples = frame->header.blocksize;
    uint32_t i;

    if ((info->audio->n+data_size) > sizeof(info->audio->buf))
        EXIT_ON_RUNTIME_ERROR_VERBOSE(ANSI_COLOR_RED"[ERR]"ANSI_COLOR_RESET"  Internal error - FLAC buffer overflown")


        // Store data in interim buffer in WAV format - i.e. Little-endian interleaved samples
        i=info->audio->n;
    for (c_samp = d_samp = 0; c_samp < samples; c_samp++)
    {
        for (c_chan = 0; c_chan < frame->header.channels; c_chan++, d_samp++)
        {
            info->audio->buf[i++]=(buf[c_chan][c_samp]&0xff);
            info->audio->buf[i++]=(buf[c_chan][c_samp]&0xff00)>>8;
            if (info->bitspersample==24)
            {
                info->audio->buf[i++]=(buf[c_chan][c_samp]&0xff0000)>>16;
            }
        }
    }
    info->audio->n=i;

    return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}



void flac_error_callback(const FLAC__StreamDecoder *dec,
                         FLAC__StreamDecoderErrorStatus status, void *data)
{
    foutput("%s", ANSI_COLOR_RED"[ERR]"ANSI_COLOR_RESET"  FLAC error callback called.\n");
}

#endif

int calc_info(fileinfo_t* info)
{
//PATCH: provided for null dividers.
    if ((info->samplerate)*(info->channels) == 0)
    {
        foutput("%s\n", ANSI_COLOR_RED"[ERR]"ANSI_COLOR_RESET"  Null audio characteristics");
        return(NO_AFMT_FOUND);
    }




// assemble numbers for the various combinations
    short int table_index=(info->bitspersample == 24)? 1 : 0 ;

    static uint16_t T[2][6][10]=     // 16-bit table
    {
        {{	2,	2000, 16,  1984,  2010,	2028, 11, 16, 0, 0 },
            {	4,	2000, 16,  1984,  2010,	2028, 11, 16, 0, 0 },
            { 12,	2004, 24,  1980,  2010,	2028, 15, 12, 0, 0 },
            { 16,	2000, 16,  1980,  2010,	2028, 11, 16, 0, 0 },
            { 20,	2000, 20,  1980,  2010, 2028, 15, 16, 0, 0 },
            { 24,	1992, 24,  1992, 1993,  2014, 10, 10, 17, 14}},
        // 24-bit table
        {{	6,	2004, 24,  1980,  2010,	2028, 15, 12,  0, 0 },
            { 12,	2004, 24,  1980,  2010,	2028, 15, 12,  0, 0 },
            { 18,	1998, 18,  1980,  2010,	2026, 15, 16,  0, 2 },
            { 24,	1992, 24,  1968,  1993,	2014, 10, 10, 17, 14 },
            { 30,	1980,  0,  1980,  2010, 2008, 15, 16,  0, 20 },
            { 36,	1980,  0,  1980,  2010, 2008, 15, 16,  0, 20 }}
    };




#define X T[table_index][info->channels-1]


    info->sampleunitsize=X[0];
    info->lpcm_payload=X[1];
    info->firstpackdecrement=X[2];
    info->SCRquantity=X[3];
    info->firstpack_audiopesheaderquantity=X[4];
    info->midpack_audiopesheaderquantity=X[5];
    info->firstpack_lpcm_headerquantity=X[6];
    info->midpack_lpcm_headerquantity=X[7];
    info->firstpack_pes_padding=X[8];
    info->midpack_pes_padding=X[9];

#undef X



    info->bytespersecond=(info->samplerate*info->bitspersample*info->channels)/8;


    switch (info->samplerate)
    {
    case 44100:
    case 48000:
        info->bytesperframe = 5*info->channels*info->bitspersample;
        break;
    case 88200:
    case 96000:
        info->bytesperframe = 10*info->channels*info->bitspersample;
        break;

    case 176400:
    case 192000:
        info->bytesperframe = 20*info->channels*info->bitspersample;
        break;

    }


    info->numsamples=(info->numbytes/info->sampleunitsize)*info->sampleunitsize/(info->channels*info->bitspersample/8);

    info->PTS_length=(90000.0*info->numsamples)/info->samplerate;

    /* Patch : padding/pruning is now done in buffers (following version S) */

    return(AFMT_WAVE);
}

#ifndef WITHOUT_FLAC

int flac_getinfo(fileinfo_t* info)
{
    FLAC__StreamDecoder* flac;
    FLAC__StreamDecoderInitStatus  result=0;

    flac=FLAC__stream_decoder_new();

    if (flac!=NULL)
    {


        /* Transition from the FLAC 1.1.2 syntax: legacy code recalled below
        *
        *	FLAC__file_decoder_set_filename(flac,info->filename);
        *	FLAC__file_decoder_set_client_data(flac,(void*)info);
        *	FLAC__file_decoder_set_write_callback(flac,flac_null_write_callback);
        *	FLAC__file_decoder_set_error_callback(flac,flac_error_callback);
        *	FLAC__file_decoder_set_metadata_callback(flac,flac_metadata_callback);
        *
        * end of legacy code */

        /* Test flac != NULL is not enough to discriminate between Ogg FLAC and native FLAC */

        if (info->type == AFMT_FLAC )

            result=/*FLAC__StreamDecoderInitStatus*/ FLAC__stream_decoder_init_file  	(
                        /*FLAC__StreamDecoder *  */ 	 flac,
                        /*FILE * */ 	info->filename,
                        /*FLAC__StreamDecoderWriteCallback */ 	flac_null_write_callback,
                        /*FLAC__StreamDecoderMetadataCallback */ 	flac_metadata_callback,
                        /*FLAC__StreamDecoderErrorCallback  */	flac_error_callback,
                        (void *) info
                    );

        else
        {
            result=/*FLAC__StreamDecoderInitStatus*/ FLAC__stream_decoder_init_ogg_file  	(
                        /*FLAC__StreamDecoder *  */ 	 flac,
                        /*FILE * */ 	info->filename,
                        /*FLAC__StreamDecoderWriteCallback */ 	flac_null_write_callback,
                        /*FLAC__StreamDecoderMetadataCallback */ 	flac_metadata_callback,
                        /*FLAC__StreamDecoderErrorCallback  */	flac_error_callback,
                        (void *) info
                    );
        }

    }
    else
    {
        EXIT_ON_RUNTIME_ERROR_VERBOSE(ANSI_COLOR_RED"[ERR]"ANSI_COLOR_RESET"  Fatal error - could not create FLAC OR OGG FLAC decoder\n")
    }


    if (result!=FLAC__STREAM_DECODER_INIT_STATUS_OK)
    {

        FLAC__stream_decoder_delete(flac);
        EXIT_ON_RUNTIME_ERROR_VERBOSE(ANSI_COLOR_RED"[ERR]"ANSI_COLOR_RESET"  Failed to initialise FLAC decoder\n");
    }

    if (!FLAC__stream_decoder_process_until_end_of_metadata(flac))
    {

        FLAC__stream_decoder_delete(flac);
        EXIT_ON_RUNTIME_ERROR_VERBOSE(ANSI_COLOR_RED"[ERR]"ANSI_COLOR_RESET"  Failed to read metadata from FLAC file\n");
    }
    FLAC__stream_decoder_finish(flac);

    if (((info->bitspersample!=16) && (info->bitspersample!=24)) || (info->channels > 2))
    {
        return(1);
    }


    info->numbytes=info->numsamples*info->channels*(info->bitspersample/8);
    calc_info(info);
    FLAC__stream_decoder_delete(flac);
    return(0);
}
#endif

#ifndef WITHOUT_FIXWAV
int fixwav_repair(fileinfo_t *info)
{
    WaveHeader  waveheader;

    char temp[strlen(info->filename)-4+1];
    memset(temp, 0, sizeof(temp));

    if (temp == NULL)
    {
        foutput("%s\n", ANSI_COLOR_RED"[ERR]"ANSI_COLOR_RESET"  Could not allocate fixwav filename string");
        return(NO_AFMT_FOUND);
    }


    strncpy(temp, info->filename, strlen(info->filename)-4);


    char *outstring=print_time(0);
    short int memory_allocation=sizeof(temp)+5+strlen(outstring)+strlen(globals.fixwav_suffix)+4+1;

    if (memory_allocation> CHAR_BUFSIZ)
            foutput("%s\n", ""ANSI_COLOR_RED"[WAR]"ANSI_COLOR_RESET"  Shortening -o filename");
	memory_allocation=Min(memory_allocation, CHAR_BUFSIZ);
    char buf[memory_allocation];
    memset(buf, 0, memory_allocation);


    snprintf(&buf[0], memory_allocation, "%s%s%s%s", temp, globals.fixwav_suffix, outstring, ".wav");

    // If new string longer than heap allocation of reference strings, cut it


    /* Default sub-options*/

    WaveData wavedata=
    {
        info->filename,
        buf,
        globals.settings.fixwav_database,  /* database path for collecting info chunks in headers */
        NULL,
        globals.fixwav_automatic, /* automatic behaviour */
        globals.fixwav_prepend, /* do not prepend a header */
        globals.fixwav_in_place, /* do not correct in place */
        globals.fixwav_cautious, /* whether to ask user about overwrite */
        globals.fixwav_interactive, /* not interactive */
        globals.fixwav_padding, /* padding */
        globals.fixwav_prune, /* prune */
        globals.fixwav_virtual_enable, /* whether header should be fixed virtually */
        0  /* repair status */,
        0, /* padbytes */
        0, /* pruned bytes */
    };


    if (globals.debugging) foutput(ANSI_COLOR_BLUE"[INF]"ANSI_COLOR_RESET"  Fixwav diagnosis for: %s\n", info->filename);

    SINGLE_DOTS

    if (fixwav(&wavedata, &waveheader) == NULL )
    {
        SINGLE_DOTS
        foutput("\n%s\n", ANSI_COLOR_BLUE"[INF]"ANSI_COLOR_RESET"  Fixwav repair was unsuccessful; file will be skipped.");

        return(NO_AFMT_FOUND);
    }


    else
    {
        SINGLE_DOTS

        info->samplerate=waveheader.sample_fq;
        info->bitspersample=(uint8_t) waveheader.bit_p_spl;
        info->channels=(uint8_t) waveheader.channels;
        info->numbytes=waveheader.data_size;
        info->file_size=info->numbytes+waveheader.header_size;
        info->header_size=waveheader.header_size;

        if (wavedata.repair == GOOD_HEADER)
        {
            foutput("%s", ANSI_COLOR_GREEN"[MSG]"ANSI_COLOR_RESET"  Proceeding with same file...\n");

            return(AFMT_WAVE_GOOD_HEADER);
        }
        else
        {
            if (!globals.fixwav_virtual_enable)
            {
                if (!wavedata.in_place)
                {
                    // info->filename is either allocated on the command-line heap itself (for free, with -g) or freshly allocated with -i
                    // with -g filenames it is not OK to free or realloc, see free_memory, one could just do info->filename=buf;
                    // tolerating here a modest memory loss in this case

                    info->filename=strdup(buf);
                }


                foutput(ANSI_COLOR_GREEN"[MSG]"ANSI_COLOR_RESET"  Proceeding with fixed file %s:\n", wavedata.outfile );
            }
            else
                foutput(ANSI_COLOR_GREEN"[MSG]"ANSI_COLOR_RESET"  Proceeding with virtual header and same file %s:\n", info->filename );
            foutput("       Bits per sample=%d, Sample frequency: %d, Bit depth:%d Channels:%d\n", waveheader.bit_p_spl, waveheader.sample_fq, waveheader.bit_p_spl, waveheader.channels );

            return(AFMT_WAVE_FIXED);
        }

    }
}
#endif

#ifndef WITHOUT_sox

char* replace_file_extension(char * filename)
{
    int l=0,s=strlen(filename);

    do
        l++;
    while ((l < s-1) && (filename[s-l] != '.'));

    if (1 == s-l)
    {
        foutput("%s\n", ANSI_COLOR_RED"[ERR]"ANSI_COLOR_RESET"  To convert to WAV SoX.needs to indentify audio format and filename.\n       Use extension of type '.format'\n");
        if (globals.debugging)  foutput("%s\n", ANSI_COLOR_BLUE"[INF]"ANSI_COLOR_RESET"  Skipping file.\n ");
        return(NULL);
    }

// Requires C99
    int size=s-l;

    char new_wav_name[size+9];


    memcpy(new_wav_name, filename, size);
    memcpy(new_wav_name+size, "_sox.wav", 8);
    new_wav_name[size+8]='\0';

    // Here, tolerating a memory leak for -g filenames.
    // -i filenames could in principle be freed.
    // filename cannot be altered directly as there the suffix increases its size and -g names cannot be reallocated

    filename=strndup(new_wav_name, size+9);

    return (filename);
}


int launch_sox(char** filename)
{

    char* new_wav_name=replace_file_extension(*filename);

    if (new_wav_name == NULL)
    {
        perror(ANSI_COLOR_RED"[ERR]"ANSI_COLOR_RESET"  SoX string suffix was not allocted");
        return(NO_AFMT_FOUND);
    }

    if (globals.debugging)
        foutput("%s       %s -->\n       %s \n", ANSI_COLOR_GREEN"[MSG]"ANSI_COLOR_RESET"  Format is neither WAV nor FLAC\n"ANSI_COLOR_BLUE"[INF]"ANSI_COLOR_RESET"  Converting to WAV with SoX...\n", *filename, new_wav_name);
        
    unlink(new_wav_name);
    errno=0;
    if (soxconvert(*filename, new_wav_name))
    {
        if (globals.debugging)  foutput("%s\n", ANSI_COLOR_BLUE"[INF]"ANSI_COLOR_RESET"  File was converted.");

        *filename=new_wav_name;

        return(AFMT_WAVE);
    }
    if (globals.debugging)  foutput("%s\n", ANSI_COLOR_BLUE"[INF]"ANSI_COLOR_RESET"  SoX could not convert file.");

    return(NO_AFMT_FOUND);

}
#endif


int extract_audio_info(fileinfo_t *info, uint8_t * header)
{
    info->type=AFMT_WAVE;

#ifndef WITHOUT_FIXWAV

    /* parsing header again with FIXWAV utility */

    if (globals.fixwav_enable)
    {
        #if 0
        _Bool silence_previous_value=globals.silence;
        // Fixwav may request interaction so triggering verbose mode, except in automatic mode (-F by default)
        if ((!globals.fixwav_automatic)  && (silence_previous_value))
        {
                globals.silence=0;
        }
        #endif
        /* This is an anti-looping sercurity: normally its is spurious, yet it might be useful in unexpected cases */

       static _Bool cut;
       if (!cut) info->type=fixwav_repair(info);
       cut=((info->type == AFMT_WAVE_FIXED) || (info->type == AFMT_WAVE_GOOD_HEADER));


        // Reverting to previous verbosity level
         #if 0
        //resetting
        if ((!globals.fixwav_automatic)  && (silence_previous_value))
        {
           globals.silence=silence_previous_value;
           initialise_c_utils(globals.silence, globals.logfile, globals.debugging, globals.journal);
        }
        #endif
    }

    else

#endif

        /* otherwise, reading header, assuming 44-byte headers */


    {
        info->samplerate=header[24]|(header[25]<<8)|(header[26]<<16)|(header[27]<<24);
        info->bitspersample=header[34];
        info->channels=header[22];//
        info->numbytes=header[40]|(header[41]<<8)|(header[42]<<16)|(header[43]<<24);


        if (info->numbytes > info->file_size - info->header_size)
        {

            foutput(""ANSI_COLOR_RED"[WAR]"ANSI_COLOR_RESET"  Expected %"PRIu64" bytes but found %"PRIu64" on disc...patching data.\n",
                    info->numbytes, info->file_size-info->header_size);
            info->numbytes = info->file_size - info->header_size;
        }

    }

    if ((info->bitspersample!=16) && (info->bitspersample!=24))
    {
        foutput("%s\n", ""ANSI_COLOR_RED"[WAR]"ANSI_COLOR_RESET"  Audio characteristics of file could not be found.");
        #ifndef WITHOUT_FIXWAV
        foutput("%s\n", "       Fixing wav header (option -F) ...");
        info->type=fixwav_repair(info);
        #else
        return(NO_AFMT_FOUND);
        #endif

    }

    // minimal patching without fiwxwav
    if (calc_info(info) == NO_AFMT_FOUND)
    {
      return(info->type=NO_AFMT_FOUND);
    }

    return(info->type);
}

int wav_getinfo(fileinfo_t* info)
{

    FILE * fp=NULL;

    fp=fopen(info->filename,"rb");

    if (info->filename == NULL)
    {
      foutput("%s\n", ANSI_COLOR_RED"[ERR]"ANSI_COLOR_RESET"  Could not open audio file: filepath pointer is null");
      if (fp == NULL)
         foutput(ANSI_COLOR_RED"[ERR]"ANSI_COLOR_RESET"  Could not open audio file %s: pointer is null\n", info->filename);
         EXIT_ON_RUNTIME_ERROR
    }
    else
    {
      if (globals.debugging) foutput(ANSI_COLOR_BLUE"[INF]"ANSI_COLOR_RESET"  Opening %s to get info\n", info->filename);
      change_directory(globals.settings.workdir);
      fp=secure_open(info->filename, "rb");
    }

    // C99 needed
    fseek(fp, 0, SEEK_SET);
    infochunk ichunk;
    memset(&ichunk, 0, sizeof(infochunk));
    uint8_t span=0;
    parse_wav_header(fp, &ichunk);
    span=ichunk.span;

    info->header_size=(span > 0) ? span + 8 : MAX_HEADER_SIZE;

    uint8_t header[info->header_size];
    memset(header, 0, info->header_size);

     /* PATCH: real size on disc is needed */
#if defined __WIN32__
    info->file_size = read_file_size(fp, (TCHAR*) info->filename);
#else
    info->file_size = read_file_size(fp, info->filename);
#endif

    fread(header, info->header_size,1,fp);
    fseek(fp, 0, SEEK_SET);

    if (info->header_size > (span=fread(header, 1, info->header_size,fp)))
    {
        foutput(ANSI_COLOR_RED"[ERR]"ANSI_COLOR_RESET"  Could not read header of size %d, just read %d character(s)\n", info->header_size, span);
        perror("       ");
        clean_exit(EXIT_FAILURE);
    }

     fclose(fp);

    // default value
    info->type=NO_AFMT_FOUND;


    if ((memcmp(header,"RIFF",4)!=0) || (memcmp(&header[8],"WAVEfmt",7)!=0))
    {
#ifndef WITHOUT_FLAC

        /* Other formats than WAV: parsing headers */
        if (memcmp(header,"fLaC",4) == 0 )
            return(info->type=AFMT_FLAC);
        else
        {
            if ((memcmp(header,"OggS",4) == 0 ) && (memcmp(header+0x17, "FLAC", 4) != 0))
               return(info->type=AFMT_OGG_FLAC);

            else
            {
#endif
#ifndef WITHOUT_sox

                if (globals.sox_enable)
                {
                    // When RIFF fmt headers are not recognized, they are processed by Sox first if -S -F is on command line then checked by fixwav
                    // yest SoX may crash for seriously mangled headers
#ifndef WITHOUT_libfixwav
                    if (!globals.fixwav_force)
                    {
#endif
                        if (launch_sox(&info->filename) == NO_AFMT_FOUND)
                           return(info->type);
                          // It is necessary to reassign info->file_size as conversion may have marginal effects on size (due to headers/meta-info)
                        else
                          // PATCH looping back to get info
                           return(info->type=wav_getinfo(info));
#ifndef WITHOUT_libfixwav    // yet without the processing tail below (preserving new header[] array and info structure)
                    }

                    else
                    {
                     // Other way round if -S -Fforce, as fixwav processes first before SoX
                      info->type=extract_audio_info(info, header);

                      switch(info->type)
                      {
                         case AFMT_WAVE_GOOD_HEADER :
                         case AFMT_WAVE_FIXED :
                            return (info->type=AFMT_WAVE);

                         default:
                           // PATCH looping back to get info
#endif
                            if (launch_sox(&info->filename) == NO_AFMT_FOUND)
                            return(info->type);
                          // It is necessary to reassign info->file_size as conversion may have marginal effects on size (due to headers/meta-info)
                            else
                          // PATCH looping back to get info
                            return(info->type=wav_getinfo(info));
                              // yet without the processing tail below (preserving new header[] array and info structure)
                      }
                   }
                }
                else

#endif
#ifndef WITHOUT_FIXWAV
                  if ((!globals.fixwav_force) && (!globals.fixwav_prepend))
#endif
                   return(info->type);
#ifndef WITHOUT_FIXWAV
                 else
                   return(info->type=extract_audio_info(info, header));
#endif

#ifndef WITHOUT_FLAC
            }
        }
#endif
    }
    // else, if suboption force is used:

    info->type=extract_audio_info(info, header);
    return(info->type);
}




int audio_open(fileinfo_t* info)
{
#ifndef WITHOUT_FLAC
    FLAC__StreamDecoderInitStatus result=0;
#endif

    info->audio=malloc(sizeof(audio_input_t));

    if (info->type==AFMT_WAVE)
    {
        info->audio->fp=fopen(info->filename,"rb");
        if (info->audio->fp==0)
        {
            return(1);
        }
#ifdef __WIN32__
        info->file_size = read_file_size(info->audio->fp, (TCHAR*) info->filename);
#else
        info->file_size = read_file_size(info->audio->fp, info->filename);
#endif
        fseek(info->audio->fp, info->header_size,SEEK_SET);


        info->audio->bytesread=0;
    }
#ifndef WITHOUT_FLAC
    else
    {
        info->audio->flac=FLAC__stream_decoder_new();
        info->audio->n=0;
        info->audio->eos=0;

        if (info->audio->flac!=NULL)
        {

            if  (info->type==AFMT_FLAC)
            {

                result=/*FLAC__StreamDecoderInitStatus*/ FLAC__stream_decoder_init_file  	(
                            /*FLAC__StreamDecoder *  */ 	 info->audio->flac,
                            /*char * */ 	info->filename,
                            /*FLAC__StreamDecoderWriteCallback */ 	flac_write_callback,
                            /*FLAC__StreamDecoderMetadataCallback */ 	flac_metadata_callback,
                            /*FLAC__StreamDecoderErrorCallback  */	flac_error_callback,
                            (void *) info
                        );
                if ((globals.debugging) && (result == FLAC__STREAM_DECODER_INIT_STATUS_OK))
                    foutput("%s\n", ANSI_COLOR_GREEN"[MSG]"ANSI_COLOR_RESET"  FLAC decoder was initialized");
            }
            else

                if  (info->type==AFMT_OGG_FLAC)
                {

                    result=/*FLAC__StreamDecoderInitStatus*/ FLAC__stream_decoder_init_ogg_file  	(
                                /*FLAC__StreamDecoder *  */ 	 info->audio->flac,
                                /*char * */ 	info->filename,
                                /*FLAC__StreamDecoderWriteCallback */ 	flac_write_callback,
                                /*FLAC__StreamDecoderMetadataCallback */ 	flac_metadata_callback,
                                /*FLAC__StreamDecoderErrorCallback  */	flac_error_callback,
                                (void *) info
                            );

                    if ((globals.debugging) && (result == FLAC__STREAM_DECODER_INIT_STATUS_OK))
                        foutput("%s\n", ANSI_COLOR_GREEN"[MSG]"ANSI_COLOR_RESET"  OGG_FLAC decoder was initialized");
                }
                else
                    EXIT_ON_RUNTIME_ERROR_VERBOSE(ANSI_COLOR_RED"[ERR]"ANSI_COLOR_RESET"  Type of file unknown")



                    if (result!=FLAC__STREAM_DECODER_INIT_STATUS_OK)
                    {
                        FLAC__stream_decoder_delete(info->audio->flac);

                        /* error diagnosis */

                        if (globals.debugging)

                            switch (result)
                            {
                            case   FLAC__STREAM_DECODER_INIT_STATUS_UNSUPPORTED_CONTAINER  :
                                foutput ("%s\n", ANSI_COLOR_RED"[ERR]"ANSI_COLOR_RESET"  The library was not compiled with support\n       for the given container format. ");
                                break;
                            case   FLAC__STREAM_DECODER_INIT_STATUS_INVALID_CALLBACKS :
                                foutput("%s\n",  ANSI_COLOR_RED"[ERR]"ANSI_COLOR_RESET"  A required callback was not supplied.");
                                break;
                            case   FLAC__STREAM_DECODER_INIT_STATUS_MEMORY_ALLOCATION_ERROR :
                                foutput("%s\n", ANSI_COLOR_RED"[ERR]"ANSI_COLOR_RESET"  An error occurred allocating memory.");
                                break;
                            case   FLAC__STREAM_DECODER_INIT_STATUS_ERROR_OPENING_FILE :
                                foutput("%s\n", ANSI_COLOR_RED"[ERR]"ANSI_COLOR_RESET"  fopen() failed in FLAC__stream_decoder_init_file()\n       or FLAC__stream_decoder_init_ogg_file(). ");
                                break;
                            case   FLAC__STREAM_DECODER_INIT_STATUS_ALREADY_INITIALIZED :
                                foutput("%s\n", ANSI_COLOR_RED"[ERR]"ANSI_COLOR_RESET"  FLAC__stream_decoder_init_*() was called when the decoder was already initialized,\n       usually because FLAC__stream_decoder_finish() was not called.");
                                break;
                            default :
                                foutput("%s\n", ANSI_COLOR_RED"[ERR]"ANSI_COLOR_RESET"  Error unknown by FLAC API.");
                            }

                        EXIT_ON_RUNTIME_ERROR_VERBOSE(ANSI_COLOR_RED"[ERR]"ANSI_COLOR_RESET"  Failed to initialise FLAC decoder\n");
                    }


            if (!FLAC__stream_decoder_process_until_end_of_metadata(info->audio->flac))
            {
                FLAC__stream_decoder_delete(info->audio->flac);
                EXIT_ON_RUNTIME_ERROR_VERBOSE( ANSI_COLOR_RED"[ERR]"ANSI_COLOR_RESET"  Failed to read metadata from FLAC file\n")

            }



        }
        else    EXIT_ON_RUNTIME_ERROR_VERBOSE(ANSI_COLOR_RED"[ERR]"ANSI_COLOR_RESET"  Could not initialise FLAC decoder")

        }
#endif

    return(0);
}




/*
For the cases of 16-bit mono and stereo, the only change is from little-endian to big-endian, so a pair of bytes 01 23
is converted to the order 23 12.  Alternatively, we describe this in terms of byte labels as

16-bit 1 or 2 channels
WAV:  0  1
AOB:   1  0

In the original dvda-author, the mono and stereo 24-bit cases were illustrated thus:

24-bit mono samples are packed as follows:
	           0   1     2   3   4   5
	WAV: 01 23 45 12 34 56
	AOB: 45 23 56 34 01 12
	             2   1   5   4    0  3

 24-bit Stereo samples are packed as follows:
		0   1     2  3    4   5    6  7    8  9  10  11
	WAV: 01 23 45 bf 60 8c 67 89 ab b7 d4 e3
	AOB: 45 23 8c 60 ab 89 e3 d4 01 bf 67 b7
	            2   1    5  4    8    7  11  10  0  3   6   9

For brevity, we use only the more compact label-based description for each of the 12 cases treated here.
The in-place transformatioin code that derives from this description was machine generated to reduce the chance for transcription errors,. Identity expressions, e.g. x[i+1] = x[i+1], are omitted.

The 12 cases are as follows. Their values are encoded in matrix S to be found in src/include/multichannel.h

 16-bit 1  channel
WAV: 0  1
AOB: 1  0

 16-bit 2  channel
WAV: 0  1
AOB: 1  0

 16-bit 3  channel
WAV: 0  1  2  3  4  5  6  7  8  9  10  11
AOB: 5  4  11  10  1  0  3  2  7  6  9  8

 16-bit 4  channel
WAV: 0  1  2  3  4  5  6  7  8  9  10  11  12  13  14  15
AOB: 5  4  7  6  13  12  15  14  1  0  3  2  9  8  11  10

 16-bit 5  channel
WAV: 0  1  2  3  4  5  6  7  8  9  10  11  12  13  14  15  16  17  18  19
AOB: 5  4  7  6  9  8  15  14  17  16  19  18  1  0  3  2  11  10  13  12

 16-bit 6  channel
WAV: 0  1  2  3  4  5  6  7  8  9  10  11  12  13  14  15  16  17  18  19  20  21  22  23
AOB: 5  4  7  6  17  16  19  18  1  0  3  2  9  8  11  10  13  12  15  14  21  20  23  22

 24-bit 1  channel
WAV: 0  1  2  3  4  5
AOB: 2  1  5  4  0  3

 24-bit 2  channel
WAV: 0  1  2  3  4  5  6  7  8  9  10  11
AOB: 2  1  5  4  8  7  11  10  0  3  6  9

 24-bit 3  channel
WAV: 0  1  2  3  4  5  6  7  8  9  10  11  12  13  14  15  16  17
AOB: 8  7  17  16  6  15  2  1  5  11  10  14  13  0   3   9   12

 24-bit 4  channel
WAV: 0  1  2  3  4  5  6  7  8  9  10  11  12  13  14  15  16  17  18  19  20  21  22  23
AOB: 8  7  11  10  20  19  23  22  6  9  18  21  2  1  5  4  14  13  17  16  0  3  12  15


 24-bit 5  channel
WAV: 0  1  2  3  4  5  6  7  8  9  10  11  12  13  14  15  16  17  18  19  20  21  22  23  24  25  26  27  28  29
AOB: 8  7  11  10  14  13  23  22  26  25  29  28  6  9  12  21  24  27  2  1  5  4  17  16  20  19  0  3  15  18

 24-bit 6  channel
WAV: 0  1  2  3  4  5  6  7  8  9  10  11  12  13  14  15  16  17  18  19  20  21  22  23  24  25  26  27  28  29  30  31  32  33  34  35
AOB: 8  7  11  10  26  25  29  28  6  9  24  27  2  1  5  4  14  13  17  16  20  19  23  22  32  31  35  34  0  3  12  15  18  21  30  33


The sequences presented above may be verified by constructing an .aob file with an easily identifiable sequence of bytes
and presenting it to a (correct) dvd-audio extraction program.  The byte order in the extracted wave file should be the inverse
of that given above.

Now follows the actual manipulation code.  Note that performing the transformation in place requires that the order of execution within a given case be respected.
*/


inline static void interleave_sample_extended(int channels, int count, uint8_t * buf)
{

    int x,i, size=channels*4;
    uint8_t _buf[size];

    switch (channels)
    {
    case 1:
    case 2:

        for (i=0;i<count;i+=2)
            x=buf[i+1], buf[i+1]=buf[i], buf[i]=x;
        break;

    default:

        for (i=0; i < count; i += size)

            permutation(buf+i,_buf, 0, channels, S, size);



        break;
    }

}


inline static void interleave_24_bit_sample_extended(int channels, int count, uint8_t * buf)

{

    int i, size=channels*6;
    uint8_t _buf[size];


    for (i=0; i < count; i += size)
        permutation(buf+i,_buf, 1, channels, S, size);


}


// Read numbytes of audio data, and convert it to DVD byte order
uint32_t audio_read(fileinfo_t* info, uint8_t* buf, uint32_t count)
{
    uint32_t n=0, bytesread=0, padbytes=0;
    static uint32_t rmdr;
    FLAC__bool result;

    //PATCH: provided for null audio characteristics, to ensure non-zero divider

    if (info->sampleunitsize == 0)
        EXIT_ON_RUNTIME_ERROR_VERBOSE(ANSI_COLOR_RED"[ERR]"ANSI_COLOR_RESET"  Sample unit size is null");

    count-= count%info->sampleunitsize;
    if (count%info->sampleunitsize)
    {
        foutput("Requested %d bytes, sampleunitsize=%"PRIu16"\n",count,info->sampleunitsize);
        fflush(stdout);
    }

    if (info->type==AFMT_WAVE)
    {
        n=fread(buf,1,count,info->audio->fp);
        if (info->audio->bytesread+n > info->numbytes)
        {
            n=info->numbytes-info->audio->bytesread;
        }
        info->audio->bytesread+=n;
        bytesread=n;

        while ((info->audio->bytesread < info->numbytes) && (bytesread < count))
        {
            n=fread(&buf[bytesread],1,count-bytesread,info->audio->fp);
            if (info->audio->bytesread+n > info->numbytes)
            {
                n=info->numbytes-info->audio->bytesread;
            }
            info->audio->bytesread+=n;
            bytesread+=n;
        }
        n=bytesread;
		//   Padding occurs here for whole number of samples (from LF version S)
        rmdr = n % info->sampleunitsize;
        padbytes = info->sampleunitsize - rmdr;
        
        #if 0
        if (rmdr)
        {
            if ((globals.padding)&&(n+padbytes < AUDIO_BUFFER_SIZE))
            {
                memset(buf+n, (globals.padding_continuous)? buf[n-1] : 0, padbytes);
                n+=padbytes;
                if (globals.debugging) foutput(""ANSI_COLOR_RED"[WAR]"ANSI_COLOR_RESET"  Padding track with %d bytes for sample count.\n       Sample unit size is %d\n",padbytes,info->sampleunitsize);
            }
            else
            if (globals.lossy_rounding)
                {
                    n-=rmdr;
                    if (globals.debugging) foutput(""ANSI_COLOR_RED"[WAR]"ANSI_COLOR_RESET"  Pruned track by %d bytes for sample count.\n       Sample unit size is %d\n",rmdr,info->sampleunitsize);
                }

        }
        #endif
        
        // end of LF version S import
        rmdr = n %2;
        
        #if 0
        if (rmdr)
        {
            if ((globals.padding)&&(n+padbytes < AUDIO_BUFFER_SIZE))
            {
                buf[n+1]=(globals.padding_continuous)? buf[n] : 0;
                n++;
                if (globals.debugging) foutput(""ANSI_COLOR_RED"[WAR]"ANSI_COLOR_RESET"  Padding track with 1 byte for evenness n= %d\n",n);
            }
            else
            if (globals.lossy_rounding)
                {
                    n--;
                    if (globals.debugging) foutput(""ANSI_COLOR_RED"[WAR]"ANSI_COLOR_RESET"  Pruned track by 1 byte for evenness n= %d\n",n);
                }
        }
        #endif

    }
#ifndef WITHOUT_FLAC

    else if ((info->type==AFMT_FLAC) || (info->type==AFMT_OGG_FLAC))
    {
        while ((info->audio->n < count) && (info->audio->eos==0))
        {
            result=FLAC__stream_decoder_process_single(info->audio->flac);

            if (result==0)
                EXIT_ON_RUNTIME_ERROR_VERBOSE(ANSI_COLOR_RED"[ERR]"ANSI_COLOR_RESET"  Fatal error decoding FLAC file\n")

                if (FLAC__stream_decoder_get_state(info->audio->flac)==FLAC__STREAM_DECODER_END_OF_STREAM)
                {
                    info->audio->eos=1;
                }
        }
        if (info->audio->n >= count)
        {
            n=count;
            memcpy(buf,info->audio->buf,count);
            memmove(info->audio->buf,&(info->audio->buf[count]),info->audio->n-count);
            info->audio->n-=count;
        }
        else
        {
            n=info->audio->n;
            memcpy(buf,info->audio->buf,info->audio->n);
            info->audio->n=0;
        }
    }
#endif



    // Convert little-endian WAV samples to big-endian MPEG LPCM samples

    if ((info->channels > 6) || (info->channels < 1))
    {

        foutput(ANSI_COLOR_RED"[ERR]"ANSI_COLOR_RESET"  problem in audio.c ! %d channels \n",info->channels);
        EXIT_ON_RUNTIME_ERROR
    }

    switch (info->bitspersample)
    {
    case 24:


        // Processing 16-bit audio
        interleave_24_bit_sample_extended(info->channels, count, buf);


        break;

    case 16:

        // Processing 16-bit audio
        interleave_sample_extended(info->channels, count, buf);

        break;

    default:

        /* 20-bit stereo samples are packed as follows:
        Packet 0: 1980 bytes
        Packets 1-: 2000 bytes

        Stored similarly to 24-bit:

        4 samples, most significant 16 bits of each sample first, in big-endian
        order, followed by 2 bytes containing the least-significant 4 bits of
        each sample.

        I'm guessing  that  20-bits are stored in the most-significant
        20-bits of the 24.
        */

        // FIX: Handle 20-bit audio and maybe convert other formats.
        foutput(ANSI_COLOR_RED"[ERR]"ANSI_COLOR_RESET"  %d bit audio is not supported\n",info->bitspersample);
        EXIT_ON_RUNTIME_ERROR
    }

    return(n);
}

int audio_close(fileinfo_t* info)
{
    if (info->type==AFMT_WAVE)
    {
        fclose(info->audio->fp);
    }
#ifndef WITHOUT_FLAC
    else if ((info->type==AFMT_FLAC) || (info->type == AFMT_OGG_FLAC))
    {
        FLAC__stream_decoder_delete(info->audio->flac);
    }
#endif
    // info->audio[i][j] will be freed before exit in free_memory.
    return(0);
}

