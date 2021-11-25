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
#include "sisci_variables.h"
#include "common.h"
#include "me.h"
#include "tables.h"



static uint32_t remote_node = 0;

/* getopt */
extern int optind;
extern char *optarg;

static void print_help()
{
  printf("Usage: ./c63server -r nodeid\n");
  printf("Commandline options:\n");
  printf("  -r                             Node id of client\n");
  printf("\n");

  exit(EXIT_FAILURE);
}

/*
*   c63_encode_image from c63enc without write frame
*/
static void c63_encode_image(struct c63_common *cm, yuv_t *image)
{

  /* Advance to next frame */
  destroy_frame(cm->refframe);
  cm->refframe = cm->curframe;

  cm->curframe = create_frame(cm, image);

  /* Check if keyframe */
  if (cm->framenum == 0 || cm->frames_since_keyframe == cm->keyframe_interval)
  {
    cm->curframe->keyframe = 1;
    cm->frames_since_keyframe = 0;

    fprintf(stderr, " (keyframe) ");
  }
  else { cm->curframe->keyframe = 0; }


  if (!cm->curframe->keyframe)
  {
    /* Motion Estimation */
    c63_motion_estimate(cm);

    /* Motion Compensation */
    c63_motion_compensate(cm);
  }

  /* DCT and Quantization */
  dct_quantize(image->Y, cm->curframe->predicted->Y, cm->padw[Y_COMPONENT],
      cm->padh[Y_COMPONENT], cm->curframe->residuals->Ydct,
      cm->quanttbl[Y_COMPONENT]);

  dct_quantize(image->U, cm->curframe->predicted->U, cm->padw[U_COMPONENT],
      cm->padh[U_COMPONENT], cm->curframe->residuals->Udct,
      cm->quanttbl[U_COMPONENT]);

  dct_quantize(image->V, cm->curframe->predicted->V, cm->padw[V_COMPONENT],
      cm->padh[V_COMPONENT], cm->curframe->residuals->Vdct,
      cm->quanttbl[V_COMPONENT]);


  /* Reconstruct frame for inter-prediction */
  dequantize_idct(cm->curframe->residuals->Ydct, cm->curframe->predicted->Y,
      cm->ypw, cm->yph, cm->curframe->recons->Y, cm->quanttbl[Y_COMPONENT]);
  dequantize_idct(cm->curframe->residuals->Udct, cm->curframe->predicted->U,
      cm->upw, cm->uph, cm->curframe->recons->U, cm->quanttbl[U_COMPONENT]);
  dequantize_idct(cm->curframe->residuals->Vdct, cm->curframe->predicted->V,
      cm->vpw, cm->vph, cm->curframe->recons->V, cm->quanttbl[V_COMPONENT]);
}


/*
*   init_c63_enc from c63enc
*/
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



int main(int argc, char **argv)
{
  int c;
  // Sisci variables
  unsigned int localAdapterNo = 0;
  sci_desc_t sd;
  sci_error_t error;
  size_t local_offset = 0;
  size_t remote_offset = 0;
  sci_dma_queue_t	dq;           // DMA queue used to transfer image data
  unsigned int max_entries = 1; // max entries inside dma queue

  // Segment used to transfer image y, u, v from client
  sci_local_segment_t localSegment;
  sci_map_t localMap;

  // Client server communication
  sci_local_segment_t localSegment_comms;
  sci_remote_segment_t remoteSegment_comms;
  sci_map_t localMap_comms;
  sci_map_t remoteMap_comms;

  // Struct comms defined in sisci_variables.h
  // comms contain cmd used to communicate and height/width parameters
  volatile struct comms *remote_comms;
  volatile struct comms *local_comms;

  // Segment used to transefer results to client ydct,udct,vdtc and macroblock
  sci_local_segment_t result_localSegment;
  sci_remote_segment_t result_remoteSegment;
  sci_map_t result_localMap;


  if (argc == 1) { print_help(); }

    while ((c = getopt(argc, argv, "h:w:o:f:i:r:")) != -1)
    {
      switch (c)
      {
        case 'r':
          remote_node = atoi(optarg);
          break;
        default:
          print_help();
          break;
      }
  }

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
  * Create prepare and set available segment for pio communication comms.
  */
  SCICreateSegment(sd,
                   &localSegment_comms,
                   SEGMENT_SERVER_COMMS,
                   sizeof(struct comms),
                   NO_CALLBACK,
                   NULL,
                   NO_FLAGS,
                   &error);
  if(error != SCI_ERR_OK){
   fprintf( stderr, "SCICreateSegment failed: %s - Error code: (0x%x)\n",
            SCIGetErrorString(error), error);
   exit(EXIT_FAILURE);
  }

  SCIPrepareSegment(localSegment_comms, localAdapterNo, NO_FLAGS, &error);
  if(error != SCI_ERR_OK){
   fprintf( stderr, "SCIPrepareSegment failed: %s - Error code: (0x%x)\n",
            SCIGetErrorString(error), error);
   exit(EXIT_FAILURE);
  }

  SCISetSegmentAvailable(localSegment_comms, localAdapterNo, NO_FLAGS, &error);
  if(error != SCI_ERR_OK){
   fprintf( stderr, "SCISetSegmentAvailable failed: %s - Error code: (0x%x)\n",
            SCIGetErrorString(error), error);
   exit(EXIT_FAILURE);
  }

   /*
   * Connect to remote segment in x86/client for pio communication comms.
   */
   do {
       SCIConnectSegment(sd,
                         &remoteSegment_comms,
                         remote_node,
                         SEGMENT_CLIENT_COMMS,
                         localAdapterNo,
                         NO_CALLBACK,
                         NULL,
                         SCI_INFINITE_TIMEOUT,        // dont time out
                         NO_FLAGS,
                         &error);
  } while (error != SCI_ERR_OK);                     //

  /*
  *   map the local and remote segments for comms
  */
  local_comms =  SCIMapLocalSegment(localSegment_comms,
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
    *   Wait until x86/client have read height/width
    *   so we can retrive them with remote_comms->packet.width/height
    */
   while(remote_comms->packet.cmd == CMD_INVALID);

   /*
   *  Create cm struct with width and height recived from x86/client
   */
   struct c63_common *cm = init_c63_enc(remote_comms->packet.width,
                                        remote_comms->packet.height);

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


  /*
  * Create, prepeare and set available local segment that for image data
  */
  SCICreateSegment(sd,
                   &localSegment,
                   SEGMENT_SERVER,
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
                  SEGMENT_SERVER_RESULT,
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

  SCISetSegmentAvailable(result_localSegment,localAdapterNo,NO_FLAGS,&error);
  if(error != SCI_ERR_OK){
    fprintf(stderr, "SCISetSegmentAvailable failed: %s -Error code: (0x%x)\n",
            SCIGetErrorString(error), error);
    exit(EXIT_FAILURE);
  }

  /*
  *   map local segment for image transfer from x86/client
  */
  local_seg =  SCIMapLocalSegment(localSegment,
                                  &localMap,
                                  local_offset,
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
  *   Connect remote segment for encoding results dma transfer to x86/client
  */
  do {
      SCIConnectSegment(sd,
                        &result_remoteSegment,
                        remote_node,
                        SEGMENT_CLIENT_RESULT,
                        localAdapterNo,
                        NO_CALLBACK,
                        NULL,
                        SCI_INFINITE_TIMEOUT,
                        NO_FLAGS,
                        &error);
  } while (error != SCI_ERR_OK);

  result_local_seg = SCIMapLocalSegment(result_localSegment,
                                        &result_localMap,
                                        0,
                                        sizeof(struct result_segment),
                                        NULL,
                                        NO_FLAGS,
                                        &error);


  /*
  *   Create a DMA queue for transfering encoding results
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

  // Create image variable to use when encoding
  yuv_t *image;
  image = malloc(sizeof(*image));
  image->Y = calloc(1, cm->padw[Y_COMPONENT]*cm->padh[Y_COMPONENT]);
  image->U = calloc(1, cm->padw[U_COMPONENT]*cm->padh[U_COMPONENT]);
  image->V = calloc(1, cm->padw[V_COMPONENT]*cm->padh[V_COMPONENT]);

  /*
  *   encoding loop
  */
  while(1)
  {
    // wait for client x86 to read and DMA image data to server
    while(local_comms->packet.cmd == CMD_INVALID);
    // Exit loop when client signals CMD_QUIT
    if(local_comms->packet.cmd == CMD_QUIT){break;}
    // set CMD_INVALID to signal the client to wait
    local_comms->packet.cmd = CMD_INVALID;

    /*
    *   use memcpy() to copy blocks of memory from local segments
    *   that has recived image data from client through DMA transfer
    */
    memcpy( image->Y,
            local_seg->Y,
            cm->padw[Y_COMPONENT]*cm->padh[Y_COMPONENT]);
    memcpy( image->U,
            local_seg->U,
            cm->padw[U_COMPONENT]*cm->padh[U_COMPONENT]);
    memcpy( image->V,
            local_seg->V,
            cm->padw[V_COMPONENT]*cm->padh[V_COMPONENT]);

    // encode frame
    c63_encode_image(cm, image);

    // copy over encoding reuslts to local result segment
    result_local_seg->keyframe = cm->curframe->keyframe;

    // copy macroblocks
    memcpy( result_local_seg->mbs[Y_COMPONENT],
            cm->curframe->mbs[Y_COMPONENT],
            cm->mb_rows * cm->mb_cols * sizeof(struct macroblock));
    memcpy( result_local_seg->mbs[U_COMPONENT],
            cm->curframe->mbs[U_COMPONENT],
            cm->mb_rows/2 * cm->mb_cols/2 * sizeof(struct macroblock));
    memcpy( result_local_seg->mbs[V_COMPONENT],
            cm->curframe->mbs[V_COMPONENT],
            cm->mb_rows/2 * cm->mb_cols/2 * sizeof(struct macroblock));

    // copy residuals
    memcpy(result_local_seg->Ydct,
           cm->curframe->residuals->Ydct, cm->ypw * cm->yph * sizeof(int16_t));
    memcpy(result_local_seg->Udct,
           cm->curframe->residuals->Udct, cm->upw * cm->uph * sizeof(int16_t));
    memcpy(result_local_seg->Vdct,
           cm->curframe->residuals->Vdct, cm->vpw * cm->vph * sizeof(int16_t));


    /*
    *   Use DMA queue to start transfer of encoding results from
    *   local result segment to remote result segment
    */
    SCIStartDmaTransfer(dq,
                        result_localSegment,
                        result_remoteSegment,
                        local_offset,
                        sizeof(struct result_segment),
                        remote_offset,
                        NO_CALLBACK,
                        NULL,
                        NO_FLAGS,
                        &error);
    if(error != SCI_ERR_OK){
      fprintf(stderr,"SCIStartDmaTransfer failed: %s - Error code: (0x%x)\n",
              SCIGetErrorString(error), error);
      exit(EXIT_FAILURE);
    }

    // wait for DMA transfer to finish
    SCIWaitForDMAQueue(dq,
                      SCI_INFINITE_TIMEOUT,
                      NO_FLAGS,
                      &error);
    if(error != SCI_ERR_OK){
      fprintf(stderr,"SCIWaitForDMAQueue failed: %s - Error code: (0x%x)\n",
              SCIGetErrorString(error), error);
      exit(EXIT_FAILURE);
    }

    // variable increments from bottom of old encode function
    ++cm->framenum;
    ++cm->frames_since_keyframe;
    /*
    * signal to x86/client to write and read next frame
    */
    remote_comms->packet.cmd = CMD_DONE;
  }
  free(image->Y);
  free(image->U);
  free(image->V);
  free(image);

  SCITerminate();
}
