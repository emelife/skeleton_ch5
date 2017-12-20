#include <linux/version.h>
#include <linux/pci.h> 
#include <linux/i2c.h>
#include <linux/videodev2.h>
#include <linux/kdev_t.h>
#include <linux/input.h>
#include <linux/notifier.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/sched.h>
#include <asm/io.h>
#include <linux/kthread.h>
#include <linux/highmem.h>
#include <linux/freezer.h>
#include <linux/v4l2-dv-timings.h>
#include <media/videobuf-vmalloc.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-device.h>
#include <media/tuner.h>
#include <media/videobuf-dma-sg.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <linux/workqueue.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-ctrls.h> 
#include <media/v4l2-event.h>
//#include <media/videobuf2-v4l2.h>
#include <media/videobuf2-dma-contig.h>
#include <linux/videodev2.h>
#include <media/v4l2-fh.h>
#include <media/videobuf2-core.h>
#include <linux/dma-mapping.h>

#define ALTERA_DMA_DESCRIPTOR_NUM 128
#define TIMEOUT 0x2000000
#define ALTERA_DMA_DID 0xE003
#define ALTERA_DMA_VID 0x1172

//#define VIDEO_NUM 5 
#define VIDEO_NUM 5
#define SKEL_TVNORMS V4L2_STD_ALL  
#define VIDEO_FORMAT V4L2_PIX_FMT_UYVY
//#define VIDEO_FORMAT V4L2_PIX_FMT_YUYV
#define UNSET (-1U)
//#define TW68_MAXBOARDS 4

 
/*
 * Supported SDTV standards. This does the same job as skel_timings_cap, but
 * for standard TV formats.
 */


struct dma_descriptor {
    u32 src_addr_ldw;
    u32 src_addr_udw;
    u32 dest_addr_ldw;
    u32 dest_addr_udw;
    u32 ctl_dma_len;
    u32 reserved[3];
} __attribute__ ((packed));

struct lite_dma_header {
    volatile u32 flags[128];
} __attribute__ ((packed));

struct lite_dma_desc_table {
    struct lite_dma_header header;
    struct dma_descriptor descriptors[ALTERA_DMA_DESCRIPTOR_NUM];
} __attribute__ ((packed));

struct skeleton_tvnorm {
	char *name;
	v4l2_std_id id;

	/* video decoder */
	unsigned int sync_control;
	unsigned int luma_control;
	unsigned int chroma_ctrl1;
	unsigned int chroma_gain;
	unsigned int chroma_ctrl2;
	unsigned int vgate_misc;

	/* video scaler */
	unsigned int h_start;
	unsigned int h_stop;
	unsigned int video_v_start;
	unsigned int video_v_stop;
	unsigned int vbi_v_start_0;
	unsigned int vbi_v_stop_0;
	unsigned int src_timing;
	unsigned int vbi_v_start_1;
};

struct skeleton_format {
	char *name;
	unsigned int fourcc;
	unsigned int depth;
	unsigned int pm;
	unsigned int vshift;	/* vertical downsampling (for planar yuv) */
	unsigned int hshift;	/* horizontal downsampling (for planar yuv) */
	unsigned int bswap:1;
	unsigned int wswap:1;
	unsigned int yuv:1;
	unsigned int planar:1;
	unsigned int uvswap:1;

};
 
struct skeleton;

struct skeleton_fh {
	struct skeleton *skel;
	v4l2_std_id std;
	struct v4l2_dv_timings timings;
	struct v4l2_pix_format format;
	unsigned input;
	struct vb2_queue queue;
	enum v4l2_buf_type type;
	unsigned int width, height;
	//set default video standard and frame size
	unsigned int dW, dH;	// default width hight
	unsigned int vd_id;

	struct mutex fh_lock;
	spinlock_t qlock;
	unsigned field;
	unsigned sequence;
	u8	fps;
	u8 interval;
	struct v4l2_framebuffer ovbuf;
	struct skeleton_format *ovfmt;
	
	struct list_head buf_list;
	struct list_head active_list;
	int buf_in_task;
	struct work_struct fb_work;
	
};
struct skel_buffer {
	//struct vb2_buffer vb;
	struct vb2_v4l2_buffer vb;
	struct list_head list; 
};

/**
 * struct skeleton - All internal data for one instance of device
 * @pdev: PCI device
 * @v4l2_dev: top-level v4l2 device struct
 * @vdev: video node structure
 * @ctrl_handler: control handler structure
 * @lock: ioctl serialization mutex
 * @std: current SDTV standard
 * @timings: current HDTV timings
 * @format: current pix format
 * @input: current video input (0 = SDTV, 1 = HDTV)
 * @queue: vb2 video capture queue
 * @qlock: spinlock controlling access to buf_list and sequence
 * @buf_list: list of buffers queued for DMA 
 * @sequence: frame sequence counter
 */



struct skeleton {
	struct list_head devlist;
	struct pci_dev *pdev;
	struct v4l2_device v4l2_dev;
	struct video_device* vdev[VIDEO_NUM];
	int nr; 
	
	struct v4l2_ctrl_handler ctrl_handler;
	struct mutex lock;
	unsigned input;
	bool video_opened[VIDEO_NUM];
	u32 ch_status[VIDEO_NUM]; //stream_stop:0x0000 stream_start:0x0001

	spinlock_t qlock;
	
	__u32 __iomem *lmmio;	
	__u8 __iomem *bmmio; 
	
	__u32 __iomem *lmmio_4;	
	__u8 __iomem *bmmio_4;
	
    struct lite_dma_desc_table *lite_table_wr_cpu_virt_addr;
    dma_addr_t lite_table_wr_bus_addr;
	
	int numpages;
    u8 *rp_wr_buffer_virt_addr;
    dma_addr_t rp_wr_buffer_bus_addr;
	char name[32];
	struct vb2_alloc_ctx	*alloc_ctx;
	struct skel_buffer * curr_buf;
	spinlock_t currlock;
	int curr_fh_id;
	int irq;
//=====work queue for FB interrupt ISR
	struct workqueue_struct * work_queue;
	struct skeleton_fh *skel_fh[VIDEO_NUM];
	struct list_head dma_task_list;
	spinlock_t tsklock;
	u32 ddr_start_addr;
	
};

extern const struct v4l2_file_operations skel_fops;
extern const struct v4l2_ioctl_ops skel_ioctl_ops;
extern u32 FB_BASE_ADDR[VIDEO_NUM];
int BFDMA_next(struct skeleton *skel,struct skel_buffer *buf);

int vdev_init(struct skeleton *skel);
void start_dma(struct skeleton *skel);
void stop_dma(struct skeleton *skel);
void start_fb(struct skeleton *skel,int fb_id);
void stop_fb(struct skeleton *skel,int fb_id);
void clear_fb_int(struct skeleton *skel,int fb_id);
//void work_handler(struct work_struct *p_work);
void release_curr_buff(struct skeleton *skel,int fb_id);
u32 get_ddr_start_addr(struct skeleton *skel,int fb_id);
void set_irq_mask(struct skeleton *skel , int mask);
u32 get_irq_status(struct skeleton *skel);


