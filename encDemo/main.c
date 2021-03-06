#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define LOG_NDEBUG 0
#define LOG_TAG "venc-file"
#include <CDX_Debug.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "venc.h"

pthread_t thread_enc_id = 0;
pthread_t thread_source_id = 0;
int mstart = 0;
FILE *test_file = NULL;
FILE *out_file = NULL;

unsigned int mwidth = 720;
unsigned int mheight = 480;

static void* SourceThread(void* pThreadData) {
  cedarv_encoder_t *venc_device = (cedarv_encoder_t *)(pThreadData);
  VencInputBuffer input_buffer;
  int result = 0;
  int has_alloc_buffer = 0;

  while(mstart) {
    
    // step 1 get alloc buffer
    if(has_alloc_buffer == 0) {

      result = venc_device->ioctrl(venc_device, VENC_CMD_GET_ALLOCATE_INPUT_BUFFER, &input_buffer);

      if(result == 0) {

        LOGD("get alloc id, input_buffer: %x", input_buffer.id);
        result = fread(input_buffer.addrvirY, 1, mwidth*mheight*3/2, test_file);

        if(result != (int)(mwidth*mheight*3/2)) {

          venc_device->ioctrl(venc_device, VENC_CMD_RETURN_ALLOCATE_INPUT_BUFFER, &input_buffer);
          LOGD("the file is end\n");
          mstart = 0;
          continue;
        }

        venc_device->ioctrl(venc_device, VENC_CMD_FLUSHCACHE_ALLOCATE_INPUT_BUFFER, &input_buffer);

        has_alloc_buffer = 1;
      } else {

        LOGD("no alloc input buffer right now");
        usleep(10*1000);

        continue;
      }
    }

        // step 2 enquene buffer to input buffer quene
    LOGD(" VENC_CMD_ENQUENE_INPUT_BUFFER");
    result = venc_device->ioctrl(venc_device, VENC_CMD_ENQUENE_INPUT_BUFFER, &input_buffer);

    if(result < 0) {

      LOGD("enquene input buffer is full");
      usleep(10*1000);
    }

    has_alloc_buffer = 0;
  }
  
  return (void*)0;
}

static void* EncoderThread(void* pThreadData) {

  cedarv_encoder_t *venc_device = (cedarv_encoder_t *)(pThreadData);
  VencInputBuffer input_buffer;
  int result = 0;

  VencOutputBuffer output_buffer;
  
  while(mstart) {
     usleep(10*10000);
    // dequene buffer from input buffer quene;
      result = venc_device->ioctrl(venc_device, VENC_CMD_DEQUENE_INPUT_BUFFER, &input_buffer);

    if(result < 0) {

      LOGV("enquene input buffer is empty");
      usleep(10*1000);
      //mstart = 0;
      continue;
    }

    result = venc_device->ioctrl(venc_device, VENC_CMD_ENCODE, &input_buffer);
    LOGD("result,encoder result: %d, input_buffer.id: %d", result, input_buffer.id);

    // return the buffer to the alloc buffer quene after encoder
    venc_device->ioctrl(venc_device, VENC_CMD_RETURN_ALLOCATE_INPUT_BUFFER, &input_buffer);
    if(result == 0) {

      memset(&output_buffer, 0, sizeof(VencOutputBuffer));
      result = venc_device->ioctrl(venc_device, VENC_CMD_GET_BITSTREAM, &output_buffer);

      fwrite(output_buffer.ptr0, 1, output_buffer.size0, out_file);

      if(output_buffer.size1) {
        fwrite(output_buffer.ptr1, 1, output_buffer.size1, out_file);
      }
      
      if(result == 0) {
        venc_device->ioctrl(venc_device, VENC_CMD_RETURN_BITSTREAM, &output_buffer);
      }
      
    }else {
      LOGD("encoder fatal error");
    }
  }
  
  return (void*)0;
}


int main(int argc, char **argv)
{
  
  if (argc!=3)
  {
    printf("Usage: ./encDemo inputfile outputfile\n");
    exit(-1);    
  }
  
  
  VencBaseConfig base_cfg;
  VencInputBuffer input_buffer;
  VencOutputBuffer output_buffer;
  VencAllocateBufferParam alloc_parm;
  VencSeqHeader header_data;
  int err = 0;

  // init base config param
  base_cfg.codectype = VENC_CODEC_H264;
  base_cfg.framerate = 25;
  base_cfg.input_width = mwidth;
  base_cfg.input_height= mheight;
  base_cfg.dst_width = mwidth;
  base_cfg.dst_height = mheight;
  base_cfg.maxKeyInterval = 25;
  base_cfg.inputformat = VENC_PIXEL_YUV420; //uv combined
  base_cfg.targetbitrate = 2*1024*1024;

  // init allocate param
  alloc_parm.buffernum = 4;

  LOGD("cedarx_hardware_init");
  cedarx_hardware_init(0);  
  
  
  cedarv_encoder_t *venc_device = cedarvEncInit();
  venc_device->ioctrl(venc_device, VENC_CMD_BASE_CONFIG, &base_cfg);
  venc_device->ioctrl(venc_device, VENC_CMD_ALLOCATE_INPUT_BUFFER, &alloc_parm);
  venc_device->ioctrl(venc_device, VENC_CMD_OPEN, 0);

  venc_device->ioctrl(venc_device, VENC_CMD_HEADER_DATA, &header_data);

  test_file = fopen(argv[1], "r");

  if(test_file == NULL) {
    LOGE("open file error");
    venc_device->ioctrl(venc_device, VENC_CMD_CLOSE, 0);
    cedarvEncExit(venc_device);
    venc_device = NULL;

    cedarx_hardware_exit(0);
    return 0;
  }

  out_file = fopen(argv[2], "wb");

  if(out_file == NULL) {
    LOGE("open file error");
    venc_device->ioctrl(venc_device, VENC_CMD_CLOSE, 0);
    cedarvEncExit(venc_device);
    venc_device = NULL;
  
    cedarx_hardware_exit(0);

    fclose(test_file);
    return 0;
  }

  fwrite(header_data.bufptr, 1, header_data.length, out_file);

  mstart = 1;

  /* create source thread */
  err = pthread_create(&thread_source_id, NULL, SourceThread, venc_device);
  if (err || !thread_source_id) {
    LOGE("Create thread_source_id fail !\n");
  }

  /* create encode thread*/
  err = pthread_create(&thread_enc_id, NULL, EncoderThread, venc_device);
  if (err || !thread_enc_id) {
    LOGE("Create thread_enc_id fail !\n");
  }

  if(thread_source_id !=0) {                 
        pthread_join(thread_source_id,NULL);
    }

  if(thread_enc_id !=0) {                 
        pthread_join(thread_enc_id,NULL);
    }

  venc_device->ioctrl(venc_device, VENC_CMD_CLOSE, 0);
  cedarvEncExit(venc_device);
  venc_device = NULL;
  
  cedarx_hardware_exit(0);

  if(test_file) {
    fclose(test_file);
  }

  if(out_file) {
    fclose(out_file);
  }

  return 0;
}

#ifdef __cplusplus
}
#endif /* __cplusplus */

