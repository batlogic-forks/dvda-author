/* ==========================================================================
*
*   repair.c
*   Original code: Pigiron 2007,
*   Revision: Fabrice Nicol 2008-2009 <fabnicol@users.sourceforge.net>
*
*   Description: Performs repairs on header and file.
*========================================================================== */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "fixwav.h"
#include "repair.h"
#include "checkParameters.h"
#include "fixwav_auxiliary.h"
#include "fixwav_manager.h"
#include "c_utils.h"
#include "winport.h"
#include "structures.h"
#include "libiberty.h"
/* global data */

extern globalData globals;


/*********************************************************************
* Function: repair_wav
*
* Purpose:  This function will analyze and repair the header
*********************************************************************/
int
repair_wav(FILE* infile, WaveData *info, WaveHeader *header )
{

  int repair=GOOD_HEADER;

  errno=0;

  uint64_t file_size=read_file_size(infile, info->infile);

  /*********************************************************************
  * The RIFF Chunk
  *********************************************************************/

  /* the first 4 bytes should be "RIFF" */
  if ( header->chunk_id == RIFF )
    {
      printf( "%s", "[MSG]  Found correct Chunk ID at offset 0\n" );
    }
  else
    {
      printf( "%s", "[INF]  Repairing Chunk ID at offset 0\n" );
      if (memmove( &(header->chunk_id), "RIFF", 4*sizeof(char) ) == NULL) return(FAIL);
      repair = BAD_HEADER;
    }

  /* The ChunkSize is the entire file size - 8 */
  if ( header->chunk_size ==  (file_size  - 8 ) )
    {
      printf( "[MSG]  Found correct Chunk Size of %"PRIu32" bytes at offset 4\n",
                 header->chunk_size );
    }
  else
    {
      printf( "[MSG]  Chunk Size of %"PRIu32" at offset 4 is incorrect: should be %"PRIu32" bytes\n[INF]  ... repairing\n", header->chunk_size, (uint32_t) file_size-8 );
      header->chunk_size = (uint32_t) file_size - 8;
      repair = BAD_HEADER;
    }

  /* The Chunk Format should be the letters "WAVE" */
  if ( header->chunk_format == WAVE )
    {
      printf("%s\n",  "[MSG]  Found correct Chunk Format at offset 8" );
    }
  else
    {
      printf("%s\n",  "[MSG]  Chunk Format at offset 8 is incorrect\n[INF]  ... repairing\n" );
      if (memmove( &(header->chunk_format), "WAVE", 4*sizeof(char)) == NULL) return(FAIL);
      repair = BAD_HEADER;
    }

  /*********************************************************************/

  /* The "fmt " Subchunk                                                */
  /*********************************************************************/

  /* The Subchunk1 ID should contain the letters "fmt " */
  if ( header->sub_chunk == FMT )
    {
      printf("%s\n",  "[MSG]  Found correct Subchunk1 ID at offset 12" );
    }
  else
    {
      printf("%s\n",  "[MSG]  Subchunk1 ID at offset 12 is incorrect\n[INF]  ... repairing" );
      // "fmt " ends in a space
      if (memmove( &(header->sub_chunk), "fmt ", 4*sizeof(char) ) == NULL) return(FAIL);
      repair = BAD_HEADER;
    }

  /* The Subchunk1 Size is 16 for PCM */
  if ( header->sc_size == 16 )
    {
      printf("[MSG]  Found correct Subchunk1 Size of %"PRIu32" bytes at offset 16\n", header->sc_size );
    }
  else
    {
      printf("%s\n",  "[MSG]  Subchunk1 Size at offset 16 is incorrect\n[INF]  ... repairing\n" );
      header->sc_size = 16;
      repair = BAD_HEADER;
    }

  /* The Subchunk1 Audio Format is 1 for PCM */
  if ( header->sc_format == 1 )
    {
      printf("%s\n",  "[MSG]  Found correct Subchunk1 Format at offset 20" );
    }
  else
    {
      printf("%s\n",  "[MSG]  Subchunk1 Format at offset 20 is incorrect\n[INF]  ... repairing" );
      header->sc_format = 1;
      repair = BAD_HEADER;
    }

  /****************************************
  *  On to core audio parameters
  *****************************************/

  if (info->automatic)
    {
      repair= (auto_control(info, header) == BAD_HEADER)? BAD_HEADER: ((repair == BAD_HEADER)? BAD_HEADER : GOOD_HEADER) ;
      printf("[MSG]  Audio characteristics found by automatic mode:\n       bits/s=%"PRIu16", sample rate=%"PRIu32", channels=%"PRIu16"\n",
                header->bit_p_spl, header->sample_fq, header->channels);
      if (repair == BAD_HEADER)
        {
          if (info->interactive)
            {
              printf("%s\n", "[INT]  Please confirm [y/n] ");
              if (!isok())
                {
                  printf("%s\n", "[INF]  Exiting automatic mode: user mode\n");
                  repair=user_control(info, header);
                }
            }
          else
            printf("%s\n", "[INT]  Non-interactive mode: assuming correct repair.");
        }
    }
  else

    repair= (user_control(info, header) == BAD_HEADER)? BAD_HEADER: (repair == BAD_HEADER)? BAD_HEADER : GOOD_HEADER ;


  printf("%s\n", "[INF]  Core audio characteristics were checked.\n       Now processing data subchunk");

  /*********************************************************************/
  /* The "data" Subchunk                                               */
  /*********************************************************************/

  /* The Subchunk2 ID is the ASCII characters "data" */
  if ( header->data_chunk == DATA )
    {
      printf("%s\n",  "[MSG]  Found correct Subchunk2 ID at offset 36" );
    }
  else
    {
      printf("%s\n",  "[MSG]  Subchunk2 ID at offset 36 is incorrect\n[INF]  ... repairing\n" );
      if (memmove( &(header->data_chunk),"data", 4*sizeof(char) ) == NULL) return(FAIL);
      repair = BAD_HEADER;
    }

  /* The Subchunk2 Size = NumSample * NumChannels * BitsPerSample/8 */
  if ( header->data_size == (file_size - header->header_size) )
    {
      printf(  "[MSG]  Found correct Subchunk2 Size of %"PRIu32" bytes at offset 40\n", header->data_size );
    }
  else
    {
      printf("[MSG]  Subchunk2 Size at offset 40 is incorrect: found %"PRIu32" bytes instead of %"PRIu32"\n[INF]  ... repairing\n",header->data_size, (uint32_t) file_size -header->header_size  );
      header->data_size = file_size - header->header_size;
      repair = BAD_HEADER;
    }

  return(repair);

}




int launch_repair(WaveData *info, WaveHeader *header, uint8_t * p)
{


  printf( "%s", "[INF]  Writing new header...\n" );

  /* if -o option is not used, fixwav will overwrite existing data; confirmation dialog */

  if (info->in_place)
    {
      if (info->cautious)
      {
          printf( "\n%s", "[INT]  Overwrite the existing file? [y/n] " );
         if (!isok())

            {   /* user's bailing */
              if ( info->repair == BAD_DATA )
                printf( "%s","[WAR]  Header may still be corrupt.\n" );
              else
                printf( "%s", "[INF]  No changes made to existing file\n" );
              return(info->repair) ;
            }
      }
    }

  /* if OK */
  /* readjusting sizes after normalization */
  /* if prepending, always not in place and header_size=0 */

  else
    {


      /* refresh header sizes
       * hence we substract the shrinking from original header to the standard one
       * to which one must add the prepending of a standard header, should it occur
       * For datasize, which was valued as =filesize from the start (case where header_size == 0), we now have to correct by substracting HEADER_SIZE */


      int32_t delta= -MAX(header->header_size-HEADER_SIZE, 0)  + (info->prepend)*HEADER_SIZE;  /* must be signed */
      header->chunk_size+=delta;
      header->data_size+=delta-(header->header_size == 0)*HEADER_SIZE;
      header->header_size=(header->header_size)? header->header_size : HEADER_SIZE;

    }

  /* write the new header at the beginning of the file */
  /* again, copying manually rather than invoking fread to ensure cross-compiler/platform portability */

  uint32_copy_reverse(p, header->chunk_id), p+=4;
  uint32_copy_reverse(p, header->chunk_size), p+=4;
  uint32_copy_reverse(p, header->chunk_format), p+=4;
  uint32_copy_reverse(p, header->sub_chunk), p+=4;
  uint32_copy_reverse(p, header->sc_size), p+=4;
  uint16_copy_reverse(p, header->sc_format), p+=2;
  uint16_copy_reverse(p, header->channels), p+=2;
  uint32_copy_reverse(p, header->sample_fq), p+=4;
  uint32_copy_reverse(p, header->byte_p_sec), p+=4;
  uint16_copy_reverse(p, header->byte_p_spl), p+=2;
  uint16_copy_reverse(p, header->bit_p_spl), p+=2;
  uint32_copy_reverse(p, header->data_chunk), p+=4;
  uint32_copy_reverse(p, header->data_size);



  return(info->repair) ;
}


int write_header(uint8_t *newheader, FILE* outfile, WaveData *info)
{

  // Only repairing headers virtually to cut down computing times (--fixwav-virtual)

  if (info->virtual) return(info->repair);

  int count=0;

  //closing and opening again for security/sync, may be a non-op//

  if ((!info->virtual) && ( (outfile == NULL) || (fclose(outfile) != 0) || ((outfile=fopen(info->outfile, "rb+")) == NULL) ))
    {
      printf("%s\n", "[ERR]  Failed to close/open file");
      return(FAIL);
    }


  // in manager.c a sanity check ensures that if (info->prepend) then !(info->in_place)
  // Otherwise copying fixed header to new file or in place, depending on option

  /* try to seek right offset depending on type of file */


  if (info->in_place)
    {
      printf("%s\n", "[INF]  Overwriting header...");
    }

  count=fwrite(newheader, HEADER_SIZE, 1, outfile ) ;


  if (count != 1)
    {
      fprintf( stderr, "%s\n", "[ERR]  Error updating wav file.");
      return(FAIL);
    }



  if (errno)
    {
      perror("[ERR]  Error in launch repair module");
      return(FAIL);
    }

  return(COPY_SUCCESS);
}
