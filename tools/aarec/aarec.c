// aarec: capture through AAudio (AudioRecord path) and report pacing.
// usage: aarec out.raw seconds [rate] [channels] [preset: 1=generic 5=camcorder 6=voicerec 9=unprocessed]
// Writes s16 interleaved PCM; prints frames delivered vs wall time.
#include <aaudio/AAudio.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9;}
int main(int argc,char**argv){
  if(argc<3){fprintf(stderr,"usage: %s out.raw sec [rate] [ch] [preset]\n",argv[0]);return 1;}
  double secs=atof(argv[2]); int rate=argc>3?atoi(argv[3]):48000, ch=argc>4?atoi(argv[4]):2, preset=argc>5?atoi(argv[5]):5;
  AAudioStreamBuilder*b; AAudio_createStreamBuilder(&b);
  AAudioStreamBuilder_setDirection(b,AAUDIO_DIRECTION_INPUT);
  AAudioStreamBuilder_setSampleRate(b,rate); AAudioStreamBuilder_setChannelCount(b,ch);
  AAudioStreamBuilder_setFormat(b,AAUDIO_FORMAT_PCM_I16);
  AAudioStreamBuilder_setInputPreset(b,preset);
  AAudioStreamBuilder_setSharingMode(b,AAUDIO_SHARING_MODE_SHARED);
  AAudioStream*s; int r=AAudioStreamBuilder_openStream(b,&s);
  if(r){fprintf(stderr,"open: %s\n",AAudio_convertResultToText(r));return 1;}
  rate=AAudioStream_getSampleRate(s); ch=AAudioStream_getChannelCount(s);
  printf("opened rate=%d ch=%d burst=%d cap=%d\n",rate,ch,AAudioStream_getFramesPerBurst(s),AAudioStream_getBufferCapacityInFrames(s));
  FILE*f=fopen(argv[1],"wb"); int n=1024; short*buf=malloc(n*ch*2);
  AAudioStream_requestStart(s);
  double t0=now(),last=t0,maxgap=0; long tot=0; int calls=0;
  while(now()-t0<secs){
    int got=AAudioStream_read(s,buf,n,1000000000LL);
    if(got<0){fprintf(stderr,"read: %s\n",AAudio_convertResultToText(got));break;}
    double t=now(); if(t-last>maxgap)maxgap=t-last; last=t;
    fwrite(buf,ch*2,got,f); tot+=got; calls++;
  }
  double el=now()-t0;
  printf("wall=%.3fs frames=%ld audio=%.3fs ratio=%.3f calls=%d maxgap=%.1fms xruns=%d\n",el,tot,(double)tot/rate,tot/(double)rate/el,calls,maxgap*1e3,AAudioStream_getXRunCount(s));
  AAudioStream_requestStop(s); AAudioStream_close(s); fclose(f); return 0;
}
