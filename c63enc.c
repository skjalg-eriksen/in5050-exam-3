#include <assert.h>
#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sisci_error.h>
#include <sisci_api.h>

#include "c63.h"
#include "c63_write.h"
#include "sisci_variables.h"
#include "tables.h"


static char *output_file, *input_file;
FILE *outfile;

static int limit_numframes = 0;

static uint32_t width;
static uint32_t height;
static uint32_t remote_node = 0;

/* getopt */
extern int optind;
extern char *optarg;

/* Read planar YUV frames with 4:2:0 chroma sub-sampling */
static yuv_t* read_yuv(FILE *file, struct c63_common *cm)
{
  size_t len = 0;
  yuv_t *image = malloc(sizeof(*image));

  /* Read Y. The size of Y is the same as the size of the image. The indices
     represents the color component (0 is Y, 1 is U, and 2 is V) */
  image->Y = calloc(1, cm->padw[Y_COMPONENT]*cm->padh[Y_COMPONENT]);
  len += fread(image->Y, 1, width*height, file);


  /* Read U. Given 4:2:0 chroma sub-sampling, the size is 1/4 of Y
     because (height/2)*(width/2) = (height*width)/4. */
  image->U = calloc(1, cm->padw[U_COMPONENT]*cm->padh[U_COMPONENT]);
  len += fread(image->U, 1, (width*height)/4, file);

  /* Read V. Given 4:2:0 chroma sub-sampling, the size is 1/4 of Y. */
  image->V = calloc(1, cm->padw[V_COMPONENT]*cm->padh[V_COMPONENT]);
  len += fread(image->V, 1, (width*height)/4, file);

  if (ferror(file))
  {
    perror("ferror");
    exit(EXIT_FAILURE);
  }

  if (feof(file))
  {
    free(image->Y);
    free(image->U);
    free(image->V);
    free(image);

    return NULL;
  }
  else if (len != width*height*1.5)
  {
    fprintf(stderr, "Reached end of file, but incorrect bytes read.\n");
    fprintf(stderr, "Wrong input? (height: %d width: %d)\n", height, width);

    free(image->Y);
    free(image->U);
    free(image->V);
    free(image);

    return NULL;
  }

  return image;
}

struct c63_common* init_c63_enc(int width, int height)
{
  int i;

  /* calloc() sets allocated memory to zero */
  struct c63_common *cm = calloc(1, sizeof(struct c63_common));

  cm->width = width;
  cm->height = height;

  cm->padw[Y_COMPONENT] = cm->ypw = (uint32_t)(ceil(width/16.0f)*16);
  cm->padh[Y_COMPONENT] = cm->yph = (uint32_t)(ceil(height/16.0f)*16);
  cm->padw[U_COMPONENT] = cm->upw = (uint32_t)(ceil(width*UX/(YX*8.0f))*8);
  cm->padh[U_COMPONENT] = cm->uph = (uint32_t)(ceil(height*UY/(YY*8.0f))*8);
  cm->padw[V_COMPONENT] = cm->vpw = (uint32_t)(ceil(width*VX/(YX*8.0f))*8);
  cm->padh[V_COMPONENT] = cm->vph = (uint32_t)(ceil(height*VY/(YY*8.0f))*8);

  cm->mb_cols = cm->ypw / 8;
  cm->mb_rows = cm->yph / 8;

  /* Quality parameters -- Home exam deliveries should have original values,
   i.e., quantization factor should be 25, search range should be 16, and the
   keyframe interval should be 100. */
  cm->qp = 25;                  // Constant quantization factor. Range: [1..50]
  cm->me_search_range = 16;     // Pixels in every direction
  cm->keyframe_interval = 100;  // Distance between keyframes

  /* Initialize quantization tables */
  for (i = 0; i < 64; ++i)
  {
    cm->quanttbl[Y_COMPONENT][i] = yquanttbl_def[i] / (cm->qp / 10.0);
    cm->quanttbl[U_COMPONENT][i] = uvquanttbl_def[i] / (cm->qp / 10.0);
    cm->quanttbl[V_COMPONENT][i] = uvquanttbl_def[i] / (cm->qp / 10.0);
  }

  return cm;
}

static void print_help()
{
  printf("Usage: ./c63enc [options] input_file\n");
  printf("Commandline options:\n");
  printf("  -h                             Height of images to compress\n");
  printf("  -w                             Width of images to compress\n");
  printf("  -o                             Output file (.c63)\n");
  printf("  -r                             Node id of server\n");
  printf("  [-f]                           Limit number of frames to encode\n");
  printf("\n");

  exit(EXIT_FAILURE);
}


int main(int argc, char **argv)
{

  yuv_t *image;

  // Sisci variables
  unsigned int localAdapterNo = 0;
  sci_error_t error;
  sci_desc_t sd;
  size_t local_offset = 0;
  size_t remote_offset = 0;
  sci_dma_queue_t	dq;           // DMA queue used to transfer image data
  unsigned int max_entries = 1; // max entries inside dma queue

  // segments used to transfer image y, u, v from client
  sci_remote_segment_t remoteSegment;
  sci_local_segment_t localSegment;
  sci_map_t localMap;

  // Client server communication
  sci_remote_segment_t remoteSegment_comms;
  sci_local_segment_t localSegment_comms;
  sci_map_t localMap_comms;
  sci_map_t remoteMap_comms;


  // struct comms defined in sisci_variables.h
  // comms contain cmd used to communicate and height/width parameters
  volatile struct comms *remote_comms;
  volatile struct comms *local_comms;

  // Segment used to transefer results to client ydct,udct,vdtc and macroblock
  sci_local_segment_t result_localSegment;
  sci_remote_segment_t result_remoteSegment;
  sci_map_t result_localMap;


  int c;
  if (argc == 1) { print_help(); }

  while ((c = getopt(argc, argv, "h:w:o:f:i:r:")) != -1)
  {
    switch (c)
    {
      case 'h':
        height = atoi(optarg);
        break;
      case 'w':
        width = atoi(optarg);
        break;
      case 'o':
        output_file = optarg;
        break;
      case 'f':
        limit_numframes = atoi(optarg);
        break;
      case 'r':
        remote_node = atoi(optarg);
        break;
      default:
        print_help();
        break;
    }
  }

  if (optind >= argc)
  {
    fprintf(stderr, "Error getting program options, try --help.\n");
    exit(EXIT_FAILURE);
  }

  outfile = fopen(output_file, "wb");

  if (outfile == NULL)
  {
    perror("fopen output file");
    exit(EXIT_FAILURE);
  }

  struct c63_common *cm = init_c63_enc(width, height);
  cm->e_ctx.fp = outfile;

  input_file = argv[optind];

  if (limit_numframes) { printf("Limited to %d frames.\n", limit_numframes); }

  FILE *infile = fopen(input_file, "rb");

  if (infile == NULL)
  {
    perror("fopen input file");
    exit(EXIT_FAILURE);
  }

  /*
  *   struct image segment for transfering image data to tegra/server with DMA
  */
  volatile struct image_segment
  {
    uint8_t *Y[cm->padw[Y_COMPONENT]*cm->padh[Y_COMPONENT]];
    uint8_t *U[cm->padw[U_COMPONENT]*cm->padh[U_COMPONENT]];
    uint8_t *V[cm->padw[V_COMPONENT]*cm->padh[V_COMPONENT]];
  } *local_seg;

  /*
  *  struct result_segment for transfering encoding results back to x86/client
  */
  volatile struct result_segment
  {
    int keyframe;

    // macroblock
    struct macroblock *mbs[COLOR_COMPONENTS][cm->mb_rows * cm->mb_cols];

    // residuals
    int16_t *Ydct[cm->ypw * cm->yph];
    int16_t *Udct[cm->upw * cm->uph];
    int16_t *Vdct[cm->vpw * cm->vph];
  } *result_local_seg;

  /* Initialize the SISCI library */
  SCIInitialize(NO_FLAGS, &error);
  if (error != SCI_ERR_OK) {
      fprintf(stderr,"SCIInitialize failed: %s\n", SCIGetErrorString(error));
      exit(EXIT_FAILURE);
  }

  /* Open a file descriptor */
  SCIOpen(&sd,NO_FLAGS,&error);
  if (error != SCI_ERR_OK) {
     fprintf(stderr, "SCIOpen failed: %s (0x%x)\n",
             SCIGetErrorString(error), error);
     exit(error);
  }



  /*
  *   reate prepare and set available segment for pio communication comms.
  */
  SCICreateSegment(sd,
                   &localSegment_comms,
                   SEGMENT_CLIENT_COMMS,
                   sizeof(struct comms),
                   NO_CALLBACK,
                   NULL,
                   NO_FLAGS,
                   &error);
   if(error != SCI_ERR_OK){
     fprintf(stderr, "SCICreateSegment failed: %s - Error code: (0x%x)\n",
             SCIGetErrorString(error), error);
     exit(EXIT_FAILURE);
   }

  SCIPrepareSegment(localSegment_comms, localAdapterNo, NO_FLAGS, &error);
  if(error != SCI_ERR_OK){
    fprintf(stderr, "SCIPrepareSegment failed: %s - Error code: (0x%x)\n",
            SCIGetErrorString(error), error);
    exit(EXIT_FAILURE);
  }
  SCISetSegmentAvailable(localSegment_comms, localAdapterNo, NO_FLAGS, &error);
  if(error != SCI_ERR_OK){
    fprintf(stderr, "SCISetSegmentAvailable failed: %s - Error code: (0x%x)\n",
            SCIGetErrorString(error), error);
    exit(EXIT_FAILURE);
  }


  /*
  * Connect to remote segment in tegra/server for pio communication comms.
  */
  do {
      SCIConnectSegment(sd,
                        &remoteSegment_comms,
                        remote_node,
                        SEGMENT_SERVER_COMMS,
                        localAdapterNo,
                        NO_CALLBACK,
                        NULL,
                        SCI_INFINITE_TIMEOUT,
                        NO_FLAGS,
                        &error);
  } while (error != SCI_ERR_OK);
  if(error != SCI_ERR_OK){
    fprintf(stderr, " failed: %s - Error code: (0x%x)\n",
            SCIGetErrorString(error), error);
    exit(EXIT_FAILURE);
  }

  /*
  *   map the local and remote segments for comms
  */
  local_comms = SCIMapLocalSegment(localSegment_comms,
                                   &localMap_comms,
                                    local_offset,
                                    sizeof(struct comms),
                                    NULL,
                                    NO_FLAGS,
                                      &error);
  if(error != SCI_ERR_OK){
    fprintf(stderr, " failed: %s - Error code: (0x%x)\n",
            SCIGetErrorString(error), error);
    exit(EXIT_FAILURE);
  }
  remote_comms = SCIMapRemoteSegment(remoteSegment_comms,
                                      &remoteMap_comms,
                                      remote_offset,
                                      sizeof(struct comms),
                                      NULL,
                                      NO_FLAGS,
                                      &error);
  if(error != SCI_ERR_OK){
    fprintf(stderr, " failed: %s - Error code: (0x%x)\n",
            SCIGetErrorString(error), error);
    exit(EXIT_FAILURE);
  }

  /*
  *   Send width and hegith to tegra/server with comms
  *   and set cmd to CMD_DONE to signal that it can stop waiting
  */
  local_comms->packet.width = width;
  local_comms->packet.height = height;
  local_comms->packet.cmd = CMD_DONE;

    /*
    *   create, prepare and set available local segment for image data
    */
  SCICreateSegment(sd,
                   &localSegment,
                   SEGMENT_CLIENT,
                   sizeof(struct image_segment),
                   NO_CALLBACK,
                   NULL,
                   NO_FLAGS,
                   &error);
  if(error != SCI_ERR_OK){
   fprintf(stderr, "SCICreateSegment failed: %s - Error code: (0x%x)\n",
           SCIGetErrorString(error), error);
   exit(EXIT_FAILURE);
  }

  SCIPrepareSegment(localSegment, localAdapterNo, NO_FLAGS, &error);
  if(error != SCI_ERR_OK){
   fprintf(stderr, "SCIPrepareSegment failed: %s - Error code: (0x%x)\n",
           SCIGetErrorString(error), error);
   exit(EXIT_FAILURE);
  }
  SCISetSegmentAvailable(localSegment, localAdapterNo, NO_FLAGS, &error);
  if(error != SCI_ERR_OK){
   fprintf(stderr, "SCISetSegmentAvailable failed: %s - Error code: (0x%x)\n",
           SCIGetErrorString(error), error);
   exit(EXIT_FAILURE);
  }

  /*
  *   create, prepare and set available local segment for encoding results
  */
  SCICreateSegment(sd,
                  &result_localSegment,
                  SEGMENT_CLIENT_RESULT,
                  sizeof(struct result_segment),
                  NO_CALLBACK,
                  NULL,
                  NO_FLAGS,
                  &error);
  if(error != SCI_ERR_OK){
    fprintf(stderr, "SCICreateSegment failed: %s - Error code: (0x%x)\n",
            SCIGetErrorString(error), error);
    exit(EXIT_FAILURE);
  }

  SCIPrepareSegment(result_localSegment, localAdapterNo, NO_FLAGS, &error);
  if(error != SCI_ERR_OK){
    fprintf(stderr, "SCIPrepareSegment failed: %s - Error code: (0x%x)\n",
            SCIGetErrorString(error), error);
    exit(EXIT_FAILURE);
  }
  SCISetSegmentAvailable(result_localSegment, localAdapterNo, NO_FLAGS, &error);
  if(error != SCI_ERR_OK){
    fprintf(stderr, "SCISetSegmentAvailable failed: %s - Error code: (0x%x)\n",
            SCIGetErrorString(error), error);
    exit(EXIT_FAILURE);
  }

  /*
  *   Connect remote segment for image data dma transfer to tegra/server
  */
  do {
     SCIConnectSegment(sd,
                       &remoteSegment,
                       remote_node,
                       SEGMENT_SERVER,
                       localAdapterNo,
                       NO_CALLBACK,
                       NULL,
                       SCI_INFINITE_TIMEOUT,
                       NO_FLAGS,
                       &error);
  } while (error != SCI_ERR_OK);

  /*
  *   map local segment for image transfer from x86/client
  */
  local_seg =   SCIMapLocalSegment(localSegment,
                                      &localMap,
                                      0,
                                      sizeof(struct image_segment),
                                      NULL,
                                      NO_FLAGS,
                                      &error);
  if(error != SCI_ERR_OK){
    fprintf(stderr, "SCIMapLocalSegment failed: %s - Error code: (0x%x)\n",
            SCIGetErrorString(error), error);
    exit(EXIT_FAILURE);
  }

  /*
  *   map local segment for encoding transfer from x86/client
  */
  result_local_seg =   SCIMapLocalSegment(result_localSegment,
                                    &result_localMap,
                                    0,
                                    sizeof(struct result_segment),
                                    NULL,
                                    NO_FLAGS,
                                    &error);
  if(error != SCI_ERR_OK){
    fprintf(stderr, "SCIMapLocalSegment failed: %s - Error code: (0x%x)\n",
            SCIGetErrorString(error), error);
    exit(EXIT_FAILURE);
  }

  /*
  *   Create a DMA queue for transfering image data
  */
  SCICreateDMAQueue(sd,
                    &dq,
                    localAdapterNo,
                    max_entries,
                    NO_FLAGS,
                    &error
                  );
  if(error != SCI_ERR_OK){
    fprintf(stderr, "SCICreateDMAQueue failed: %s - Error code: (0x%x)\n",
    SCIGetErrorString(error), error);
    exit(EXIT_FAILURE);
  }

  // create cm struct for variables we need in c63_write
  cm->curframe = malloc(sizeof(struct frame));
  cm->curframe ->residuals = malloc(sizeof(dct_t));
  cm->curframe ->residuals->Ydct = calloc(cm->ypw * cm->yph, sizeof(int16_t));
  cm->curframe ->residuals->Udct = calloc(cm->upw * cm->uph, sizeof(int16_t));
  cm->curframe ->residuals->Vdct = calloc(cm->vpw * cm->vph, sizeof(int16_t));

  cm->curframe ->mbs[Y_COMPONENT] =
    calloc(cm->mb_rows * cm->mb_cols, sizeof(struct macroblock));
  cm->curframe ->mbs[U_COMPONENT] =
    calloc(cm->mb_rows/2 * cm->mb_cols/2, sizeof(struct macroblock));
  cm->curframe ->mbs[V_COMPONENT] =
    calloc(cm->mb_rows/2 * cm->mb_cols/2, sizeof(struct macroblock));

  /*
  *   read,remote-encode,write loop
  */
  int numframes = 0;
  while (1)
  {

    //remote_comms->packet.cmd = CMD_INVALID;
    local_comms->packet.cmd = CMD_INVALID;

    // read image
    image = read_yuv(infile, cm);
    if (!image) { break; }

    /*
    *   use memcpy() to copy blocks of memory from image to local segment
    */
    memcpy(local_seg->Y, image->Y, cm->padw[Y_COMPONENT]*cm->padh[Y_COMPONENT]);
    memcpy(local_seg->U, image->U, cm->padw[U_COMPONENT]*cm->padh[U_COMPONENT]);
    memcpy(local_seg->V, image->V, cm->padw[V_COMPONENT]*cm->padh[V_COMPONENT]);

    /*
    *   Use DMA queue to start DMA transfer of image data from
    *   local segment to remote segment
    */
    SCIStartDmaTransfer(dq,
                        localSegment,
                        remoteSegment,
                        local_offset,
                        sizeof(struct image_segment),
                        remote_offset,
                        NO_CALLBACK,
                        NULL,
                        NO_FLAGS,
                        &error);

    if(error != SCI_ERR_OK){
      fprintf(stderr, "SCIStartDmaTransfer failed: %s - Error code: (0x%x)\n",
      SCIGetErrorString(error), error);
      exit(EXIT_FAILURE);
    }

    // wait for DMA transfer to finish
    SCIWaitForDMAQueue(dq,
                      SCI_INFINITE_TIMEOUT,
                      NO_FLAGS,
                      &error);
    if(error != SCI_ERR_OK){
      fprintf(stderr, "SCIWaitForDMAQueue failed: %s - Error code: (0x%x)\n",
      SCIGetErrorString(error), error);
      exit(EXIT_FAILURE);
    }


    printf("Encoding frame %d, ", numframes);
    /*
    * signal to tegra/server to start encoding the image data
    */
    remote_comms->packet.cmd = CMD_DONE;

    /*
    * wait for tegra/server to finish encoding
    */
    while(local_comms->packet.cmd != CMD_DONE);


    /*
    *   use memcpy() to copy blocks of memory from local segments
    *   that has recived encoding results from server through DMA transfer
    */
    cm->curframe->keyframe = result_local_seg->keyframe;

    // macroblocks
    memcpy( cm->curframe->mbs[Y_COMPONENT],
            result_local_seg->mbs[Y_COMPONENT],
            cm->mb_rows * cm->mb_cols * sizeof(struct macroblock));
    memcpy( cm->curframe->mbs[U_COMPONENT],
            result_local_seg->mbs[U_COMPONENT],
            cm->mb_rows/2 * cm->mb_cols/2 * sizeof(struct macroblock));
    memcpy( cm->curframe->mbs[V_COMPONENT],
            result_local_seg->mbs[V_COMPONENT],
            cm->mb_rows/2 * cm->mb_cols/2 * sizeof(struct macroblock));

    // residuals
    memcpy( cm->curframe->residuals->Ydct,
            result_local_seg->Ydct,
            cm->ypw * cm->yph * sizeof(int16_t));
    memcpy( cm->curframe->residuals->Udct,
            result_local_seg->Udct,
            cm->upw * cm->uph * sizeof(int16_t));
    memcpy( cm->curframe->residuals->Vdct,
            result_local_seg->Vdct,
            cm->vpw * cm->vph * sizeof(int16_t));

    // write_frame
    write_frame(cm);
    printf("Done!\n");
    ++numframes;
    if (limit_numframes && numframes >= limit_numframes) { break; }
  }

  /*
  * set cmd to CMD_QUIT to signal the tegra/server to quit
  */
  remote_comms->packet.cmd = CMD_QUIT;

  fclose(outfile);
  fclose(infile);

  SCITerminate();

  //int i, j;
  //for (i = 0; i < 2; ++i)
  //{
  //  printf("int freq[] = {");
  //  for (j = 0; j < ARRAY_SIZE(frequencies[i]); ++j)
  //  {
  //    printf("%d, ", frequencies[i][j]);
  //  }
  //  printf("};\n");
  //}

  return EXIT_SUCCESS;
}
