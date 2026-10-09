#ifndef SHA1_MINI_H
#define SHA1_MINI_H
#include <stdint.h>
#include <string.h>

// SHA-1 compacto (dominio público, estilo Steve Reid) + HMAC-SHA1.
// Suficiente para TOTP; no usar para nada que exija seguridad fuerte.
typedef struct { uint32_t state[5]; uint32_t count[2]; uint8_t buffer[64]; } SHA1_CTX;

static inline uint32_t sha1_rol(uint32_t v, int b){ return (v<<b)|(v>>(32-b)); }

static void sha1_transform(uint32_t state[5], const uint8_t buffer[64]){
  uint32_t a,b,c,d,e,w[80];
  for(int i=0;i<16;i++) w[i]=(buffer[i*4]<<24)|(buffer[i*4+1]<<16)|(buffer[i*4+2]<<8)|buffer[i*4+3];
  for(int i=16;i<80;i++) w[i]=sha1_rol(w[i-3]^w[i-8]^w[i-14]^w[i-16],1);
  a=state[0];b=state[1];c=state[2];d=state[3];e=state[4];
  for(int i=0;i<80;i++){
    uint32_t f,k;
    if(i<20){f=(b&c)|((~b)&d);k=0x5A827999;}
    else if(i<40){f=b^c^d;k=0x6ED9EBA1;}
    else if(i<60){f=(b&c)|(b&d)|(c&d);k=0x8F1BBCDC;}
    else {f=b^c^d;k=0xCA62C1D6;}
    uint32_t t=sha1_rol(a,5)+f+e+k+w[i];
    e=d;d=c;c=sha1_rol(b,30);b=a;a=t;
  }
  state[0]+=a;state[1]+=b;state[2]+=c;state[3]+=d;state[4]+=e;
}
static void sha1_init(SHA1_CTX*c){c->state[0]=0x67452301;c->state[1]=0xEFCDAB89;c->state[2]=0x98BADCFE;c->state[3]=0x10325476;c->state[4]=0xC3D2E1F0;c->count[0]=c->count[1]=0;}
static void sha1_update(SHA1_CTX*c,const uint8_t*data,uint32_t len){
  uint32_t i,j=(c->count[0]>>3)&63;
  if((c->count[0]+=len<<3)<(len<<3))c->count[1]++;
  c->count[1]+=len>>29;
  if(j+len>63){i=64-j;memcpy(&c->buffer[j],data,i);sha1_transform(c->state,c->buffer);
    for(;i+63<len;i+=64)sha1_transform(c->state,&data[i]);j=0;} else i=0;
  memcpy(&c->buffer[j],&data[i],len-i);
}
static void sha1_final(uint8_t digest[20],SHA1_CTX*c){
  uint8_t fin[8];
  for(int i=0;i<8;i++)fin[i]=(uint8_t)(c->count[(i>=4?0:1)]>>((3-(i&3))*8));
  uint8_t ch=0x80;sha1_update(c,&ch,1);
  ch=0x00;while(((c->count[0]>>3)&63)!=56)sha1_update(c,&ch,1);
  sha1_update(c,fin,8);
  for(int i=0;i<20;i++)digest[i]=(uint8_t)(c->state[i>>2]>>((3-(i&3))*8));
}
static void hmac_sha1(const uint8_t*key,int klen,const uint8_t*msg,int mlen,uint8_t out[20]){
  uint8_t k[64],ip[64],op[64],inner[20];
  SHA1_CTX c;
  if(klen>64){sha1_init(&c);sha1_update(&c,key,klen);sha1_final(k,&c);klen=20;memset(k+20,0,44);}
  else {memcpy(k,key,klen);memset(k+klen,0,64-klen);}
  for(int i=0;i<64;i++){ip[i]=k[i]^0x36;op[i]=k[i]^0x5c;}
  sha1_init(&c);sha1_update(&c,ip,64);sha1_update(&c,msg,mlen);sha1_final(inner,&c);
  sha1_init(&c);sha1_update(&c,op,64);sha1_update(&c,inner,20);sha1_final(out,&c);
}

// Base32 (RFC 4648), ignora espacios y '='.
static int base32_decode(const char*in,uint8_t*out,int outcap){
  int buf=0,bits=0,n=0;
  for(const char*p=in;*p;p++){
    char ch=*p; int v;
    if(ch>='A'&&ch<='Z')v=ch-'A';
    else if(ch>='a'&&ch<='z')v=ch-'a';
    else if(ch>='2'&&ch<='7')v=ch-'2'+26;
    else continue; // espacios, '=', guiones...
    buf=(buf<<5)|v; bits+=5;
    if(bits>=8){bits-=8; if(n<outcap) out[n++]=(uint8_t)((buf>>bits)&0xFF);}
  }
  return n;
}

// TOTP RFC 6238 (SHA1, 6 dígitos, paso 30 s).
static uint32_t totp_at(const char*b32secret,uint64_t unixtime,int step,int digits){
  uint8_t key[64]; int klen=base32_decode(b32secret,key,sizeof(key));
  uint64_t t=unixtime/step;
  uint8_t msg[8]; for(int i=7;i>=0;i--){msg[i]=(uint8_t)(t&0xFF);t>>=8;}
  uint8_t hs[20]; hmac_sha1(key,klen,msg,8,hs);
  int off=hs[19]&0x0f;
  uint32_t bin=((hs[off]&0x7f)<<24)|(hs[off+1]<<16)|(hs[off+2]<<8)|hs[off+3];
  uint32_t mod=1; for(int i=0;i<digits;i++)mod*=10;
  return bin%mod;
}
#endif
