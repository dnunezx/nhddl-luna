// Original LUNA PS1 library metadata reader, 2026.
#include "luna_psxcore_library.h"
#include "psxcore_vcd.h"
#include "psxcore_hash.h"
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint32_t le32(const unsigned char *p) {
  return p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static uint32_t be32(const unsigned char *p) {
  return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3];
}
static int sector(FILE *file, uint64_t bytes, uint32_t lba, unsigned char out[2048]) {
  unsigned char raw[NP_VCD_SECTOR_BYTES];
  uint64_t offset = NP_VCD_DATA_OFFSET + (uint64_t)lba * sizeof(raw);
  static const unsigned char sync[12] = {0,255,255,255,255,255,255,255,255,255,255,0};
  if (offset > bytes || sizeof(raw) > bytes-offset || offset > LONG_MAX ||
      fseek(file, (long)offset, SEEK_SET) || fread(raw, 1, sizeof(raw), file) != sizeof(raw) ||
      memcmp(raw, sync, sizeof(sync))) return -1;
  unsigned start = raw[15] == 1 ? 16 : raw[15] == 2 ? 24 : 0;
  if (!start || (start == 24 && ((raw[18]&0x20) || memcmp(raw+16,raw+20,4)))) return -1;
  memcpy(out,raw+start,2048);
  return 0;
}
static int serialValid(const char *s) {
  for (unsigned i=0;i<11;++i) {
    if (i==4) { if (s[i]!='_') return 0; }
    else if (i==8) { if (s[i]!='.') return 0; }
    else if (i<4) { if (s[i]<'A'||s[i]>'Z') return 0; }
    else if (s[i]<'0'||s[i]>'9') return 0;
  }
  return 1;
}
static void bootSerial(const char *text, size_t bytes, char id[12]) {
  for (size_t p=0;p+4<bytes;++p) {
    if (p && text[p-1]!='\n' && text[p-1]!='\r') continue;
    while (p<bytes && (text[p]==' '||text[p]=='\t')) ++p;
    if (p+4>=bytes || memcmp(text+p,"BOOT",4)) continue;
    p+=4;
    while (p<bytes && (text[p]==' '||text[p]=='\t')) ++p;
    if (p==bytes || text[p++]!='=') continue;
    size_t end=p;
    while (end<bytes && text[end]!='\n' && text[end]!='\r') ++end;
    for (;p+11<=end;++p) if (serialValid(text+p)) {
      memcpy(id,text+p,11); id[11]=0; return;
    }
  }
}
static void readSerial(FILE *file, uint64_t bytes, char id[12]) {
  unsigned char data[2048];
  if (sector(file,bytes,16,data) || data[0]!=1 || memcmp(data+1,"CD001",5) || data[6]!=1) return;
  const unsigned char *r=data+156;
  if (r[0]<34 || !(r[25]&2) || le32(r+2)!=be32(r+6) || le32(r+10)!=be32(r+14)) return;
  uint32_t root=le32(r+2), length=le32(r+10);
  if (!length || length>65536) return;
  for (unsigned block=0;block<(length+2047)/2048;++block) {
    if (root>UINT32_MAX-block || sector(file,bytes,root+block,data)) return;
    unsigned limit=length-block*2048; if(limit>2048)limit=2048;
    for (unsigned p=0;p<limit;) {
      r=data+p; unsigned n=r[0]; if(!n)break;
      if(n<34 || n>limit-p || r[32]>n-33)return;
      if(r[32]==12 && !memcmp(r+33,"SYSTEM.CNF;1",12) && !(r[25]&2) &&
         le32(r+2)==be32(r+6) && le32(r+10)==be32(r+14)) {
        uint32_t extent=le32(r+2), count=le32(r+10);
        char text[4096]; if(!count || count>sizeof(text))return;
        for(unsigned i=0;i<(count+2047)/2048;++i) {
          if(extent>UINT32_MAX-i || sector(file,bytes,extent+i,data))return;
          unsigned take=count-i*2048; if(take>2048)take=2048;
          memcpy(text+i*2048,data,take);
        }
        bootSerial(text,count,id); return;
      }
      p+=n;
    }
  }
}
const char *lunaPsxDiscInfo(const char *path, char titleID[12]) {
  if(!path || !titleID)return "disc-input";
  titleID[0]=0;
  FILE *file=fopen(path,"rb"); if(!file)return "disc-open";
  const char *error=NULL;
  unsigned char header[NP_VCD_HEADER_BYTES];
  if(fseek(file,0,SEEK_END))error="disc-size";
  long bytes=error?-1:ftell(file);
  if(bytes<0 || (uint64_t)bytes>INT32_MAX)error="disc-size";
  if(!error && (fseek(file,0,SEEK_SET) || fread(header,1,sizeof(header),file)!=sizeof(header)))error="disc-read";
  if(!error)error=np_vcd_check(header,sizeof(header),(uint64_t)bytes,NULL);
  if(!error) {
    readSerial(file,(uint64_t)bytes,titleID);
    if(!titleID[0]) {
      // A metadata-only identity for unidentified discs. Saves use PSXCore's
      // full image digest, independent of artwork IDs and filenames.
      NpSha256 hash; unsigned char digest[32],data[4096];
      np_sha256_init(&hash); np_sha256_update(&hash,header,sizeof(header));
      if(!fseek(file,NP_VCD_DATA_OFFSET,SEEK_SET)) {
        for(unsigned i=0;i<16;++i) {
          size_t n=fread(data,1,sizeof(data),file);
          np_sha256_update(&hash,data,n); if(n<sizeof(data))break;
        }
      }
      np_sha256_final(&hash,digest);
      snprintf(titleID,12,"PSX%02X%02X%02X%02X",digest[0],digest[1],digest[2],digest[3]);
    }
  }
  int readError=ferror(file), closed=fclose(file);
  if(readError || closed)error="disc-read";
  if(error)titleID[0]=0;
  return error;
}
