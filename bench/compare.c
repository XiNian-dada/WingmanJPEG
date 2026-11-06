/* MIT, WingmanKVM contributors, 2026. Standalone 3.2.1 differential benchmark.
 * Both archives and native wrappers are symbol-prefixed. No system library,
 * capture device or running service is accessed. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <jpeglib.h>

#define APIS(X) \
    X(jpeg_std_error) X(jpeg_CreateCompress) X(jpeg_mem_dest) \
    X(jpeg_set_defaults) X(jpeg_set_quality) X(jpeg_start_compress) \
    X(jpeg_write_scanlines) X(jpeg_finish_compress) X(jpeg_destroy_compress) \
    X(jpeg_CreateDecompress) X(jpeg_mem_src) X(jpeg_read_header) \
    X(jpeg_start_decompress) X(jpeg_read_scanlines) X(jpeg_finish_decompress) \
    X(jpeg_destroy_decompress)
#define DECLARE(name) extern __typeof__(name) stock_##name; extern __typeof__(name) flat_##name;
APIS(DECLARE)
#undef DECLARE
#define CALL(name) (mode ? flat_##name : stock_##name)
extern const unsigned char flat_wk_jpeg_flat_marker;
typedef int (*processor)(const unsigned char *,size_t,int,int,int,int,
                         unsigned char **,size_t *,char *,size_t);
extern int stock_wk_jpeg_process(const unsigned char *,size_t,int,int,int,int,unsigned char **,size_t *,char *,size_t);
extern int flat_wk_jpeg_process(const unsigned char *,size_t,int,int,int,int,unsigned char **,size_t *,char *,size_t);
extern void stock_wk_jpeg_free(unsigned char *);
extern void flat_wk_jpeg_free(unsigned char *);

enum { MAX_ROUNDS=101, WARMUP=4 };
static void fail(const char *message) {fprintf(stderr,"%s\n",message);exit(2);}
static double now(void) {
    struct timespec t;
    if(clock_gettime(CLOCK_MONOTONIC,&t))fail("clock_gettime failed");
    return t.tv_sec+t.tv_nsec*1e-9;
}
static int order(const void *a,const void *b) {
    double x=*(const double *)a,y=*(const double *)b;
    return (x>y)-(x<y);
}
static uint64_t hash(const unsigned char *data,size_t length) {
    uint64_t result=UINT64_C(14695981039346656037);
    for(size_t i=0;i<length;i++){result^=data[i];result*=UINT64_C(1099511628211);}
    return result;
}
static unsigned char *encode(const unsigned char *rgb,unsigned width,unsigned height,
                             int mode,int quality,int optimize,size_t *length) {
    struct jpeg_compress_struct c={0};struct jpeg_error_mgr e;
    unsigned char *out=NULL;unsigned long size=0;
    c.err=CALL(jpeg_std_error)(&e);
    CALL(jpeg_CreateCompress)(&c,JPEG_LIB_VERSION,sizeof(c));
    CALL(jpeg_mem_dest)(&c,&out,&size);
    c.image_width=width;c.image_height=height;c.input_components=3;c.in_color_space=JCS_RGB;
    CALL(jpeg_set_defaults)(&c);CALL(jpeg_set_quality)(&c,quality,TRUE);
    for(int i=0;i<3;i++)c.comp_info[i].h_samp_factor=c.comp_info[i].v_samp_factor=1;
    c.dct_method=JDCT_ISLOW;c.optimize_coding=optimize;
    if(mode)c.client_data=(void *)&flat_wk_jpeg_flat_marker;
    CALL(jpeg_start_compress)(&c,TRUE);
    while(c.next_scanline<height) {
        JSAMPROW row=(JSAMPROW)(rgb+(size_t)c.next_scanline*width*3);
        if(CALL(jpeg_write_scanlines)(&c,&row,1)!=1)fail("encoder suspended");
    }
    CALL(jpeg_finish_compress)(&c);CALL(jpeg_destroy_compress)(&c);
    *length=size;return out;
}
static unsigned char *decode(const unsigned char *input,size_t length,unsigned *width,unsigned *height) {
    struct jpeg_decompress_struct d={0};struct jpeg_error_mgr e;
    d.err=stock_jpeg_std_error(&e);stock_jpeg_CreateDecompress(&d,JPEG_LIB_VERSION,sizeof(d));
    stock_jpeg_mem_src(&d,input,(unsigned long)length);
    if(stock_jpeg_read_header(&d,TRUE)!=JPEG_HEADER_OK)fail("incomplete source");
    d.out_color_space=JCS_RGB;d.dct_method=JDCT_ISLOW;
    stock_jpeg_start_decompress(&d);
    *width=d.output_width;*height=d.output_height;
    if(!*width||!*height||*width>8192||*height>8192)fail("invalid benchmark dimensions");
    unsigned char *rgb=malloc((size_t)(*width)*(*height)*3);
    if(!rgb)fail("RGB allocation failed");
    while(d.output_scanline<*height) {
        JSAMPROW row=rgb+(size_t)d.output_scanline*(*width)*3;
        if(stock_jpeg_read_scanlines(&d,&row,1)!=1)fail("decoder suspended");
    }
    stock_jpeg_finish_decompress(&d);
    if(e.num_warnings)fail("source JPEG produced warnings");
    stock_jpeg_destroy_decompress(&d);return rgb;
}
static unsigned char *read_file(const char *name,size_t *length) {
    FILE *f=fopen(name,"rb");if(!f)fail("cannot open input JPEG");
    if(fseek(f,0,SEEK_END))fail("cannot seek input");
    long size=ftell(f);if(size<=0||size>32*1024*1024)fail("invalid input length");
    rewind(f);unsigned char *data=malloc((size_t)size);if(!data)fail("input allocation failed");
    if(fread(data,1,(size_t)size,f)!=(size_t)size)fail("cannot read input");
    fclose(f);*length=(size_t)size;return data;
}
static unsigned char *synthetic(const char *pattern,unsigned width,unsigned height) {
    unsigned char *rgb=malloc((size_t)width*height*3);if(!rgb)fail("fixture allocation failed");
    uint32_t seed=0xa135ce42;
    for(unsigned y=0;y<height;y++)for(unsigned x=0;x<width;x++) {
        unsigned char *p=rgb+((size_t)y*width+x)*3;
        if(!strcmp(pattern,"flat")){p[0]=18;p[1]=22;p[2]=28;}
        else if(!strcmp(pattern,"sparse")||!strcmp(pattern,"text")) {
            int glyph=x%11<7&&y%22<14&&(x/11+y/22)%3!=0
                &&(strcmp(pattern,"sparse")||(x<900&&y<300));
            p[0]=glyph?210:18;p[1]=glyph?80+y/8%170:22;p[2]=glyph?100:28;
        } else if(!strcmp(pattern,"gradient")) {
            p[0]=x*255/width;p[1]=y*255/height;p[2]=(x+y)%256;
        } else if(!strcmp(pattern,"noise")) {
            seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;
            p[0]=seed;p[1]=seed>>8;p[2]=seed>>16;
        } else fail("unknown synthetic pattern");
    }
    return rgb;
}
static void print_times(const char *name,double *times,int rounds,size_t bytes) {
    double sorted[MAX_ROUNDS];memcpy(sorted,times,(size_t)rounds*sizeof(double));
    qsort(sorted,(size_t)rounds,sizeof(double),order);
    int p95=(95*rounds+99)/100-1;
    printf("\"%s\":{\"median_ms\":%.6f,\"p95_ms\":%.6f,\"bytes\":%zu,\"samples_ms\":[",
           name,sorted[rounds/2],sorted[p95],bytes);
    for(int i=0;i<rounds;i++)printf("%s%.6f",i?",":"",times[i]);
    printf("]}");
}
int main(int argc,char **argv) {
    if(argc<5||argc>6)fail("usage: compare PATTERN QUALITY HUFFMAN ROUNDS [INPUT.jpg]");
    const char *pattern=argv[1];int quality=atoi(argv[2]),optimize=atoi(argv[3]),rounds=atoi(argv[4]);
    if(quality<1||quality>100||optimize<0||optimize>1||rounds<3||rounds>MAX_ROUNDS)fail("invalid options");
    unsigned width=1920,height=1080;size_t input_length=0;
    unsigned char *input,*rgb;
    if(!strcmp(pattern,"photo")) {
        if(argc!=6)fail("photo requires an input path");
        input=read_file(argv[5],&input_length);
    } else {
        rgb=synthetic(pattern,width,height);
        input=encode(rgb,width,height,0,95,0,&input_length);free(rgb);
    }
    rgb=decode(input,input_length,&width,&height);
    double times[4][MAX_ROUNDS];size_t sizes[4]={0};
    processor processors[2]={stock_wk_jpeg_process,flat_wk_jpeg_process};
    for(int round=-WARMUP;round<rounds;round++) {
        for(int phase=0;phase<2;phase++) {
            unsigned char *outputs[2]={NULL,NULL};size_t lengths[2]={0,0};
            for(int step=0;step<2;step++) {
                int mode=(step+round+WARMUP)%2;int index=phase*2+mode;
                double started=now();
                if(!phase)outputs[mode]=encode(rgb,width,height,mode,quality,optimize,&lengths[mode]);
                else {
                    char error[256]={0};
                    int status=processors[mode](input,input_length,quality,0,mode,optimize,
                                                 &outputs[mode],&lengths[mode],error,sizeof(error));
                    if(status)fail(error);
                }
                double elapsed=(now()-started)*1000;
                if(round>=0)times[index][round]=elapsed;
                sizes[index]=lengths[mode];
            }
            if(lengths[0]!=lengths[1]||memcmp(outputs[0],outputs[1],lengths[0]))fail("stock/screen JPEG bytes differ");
            if(!phase){free(outputs[0]);free(outputs[1]);}
            else {stock_wk_jpeg_free(outputs[0]);flat_wk_jpeg_free(outputs[1]);}
        }
    }
    printf("{\"pattern\":\"%s\",\"quality\":%d,\"optimize_huffman\":%s,\"width\":%u,\"height\":%u,",
           pattern,quality,optimize?"true":"false",width,height);
    printf("\"rounds\":%d,\"warmup_rounds\":%d,\"input_bytes\":%zu,\"input_fnv64\":\"%016llx\",\"byte_exact\":true,",
           rounds,WARMUP,input_length,(unsigned long long)hash(input,input_length));
    const char *names[4]={"encode_stock","encode_screen","native_stock","native_screen"};
    for(int i=0;i<4;i++){if(i)printf(",");print_times(names[i],times[i],rounds,sizes[i]);}
    puts("}");free(rgb);free(input);return 0;
}
