#include <linux/init.h>
//#include <linux/list.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/sort.h>
#include <linux/kthread.h>
#include <linux/v4l2-dv-timings.h>
#include <media/videobuf2-dma-sg.h>
#include <media/v4l2-common.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-dma-contig.h>
#include <media/v4l2-ioctl.h> 
#include <media/v4l2-dv-timings.h>
#include <linux/spinlock.h>
#include "altera_dma.h"
#include "skel.h"

#define MIN(a,b) ((a) < (b) ? (a) : (b))

struct list_head skel_devlist;
struct mutex skel_devlist_lock; 
static DEFINE_MUTEX(f_lock);
int TW68_no_overlay;
int start_flag = 0;
int i_test = 0;
int first_time;
extern bool have_dma;
spinlock_t statuslock;
int dma_task_active = 0;
int playch_count = 0;
extern spinlock_t chlock;
extern struct task_struct *dma_task;
//u32 count_dma = 0;
/*
 * HDTV: this structure has the capabilities of the HDTV receiver.
 * It is used to constrain the huge list of possible formats based
 * upon the hardware capabilities.
 */
static const struct v4l2_dv_timings_cap skel_timings_cap = {
	.type = V4L2_DV_BT_656_1120,
	/* keep this initialization for compatibility with GCC < 4.4.6 */
	.reserved = { 0 },
	V4L2_INIT_BT_TIMINGS(
		720, 1920,		/* min/max width */
		480, 1080,		/* min/max height */
		27000000, 74250000,	/* min/max pixelclock*/
		V4L2_DV_BT_STD_CEA861,	/* Supported standards */
		/* capabilities */
		V4L2_DV_BT_CAP_INTERLACED | V4L2_DV_BT_CAP_PROGRESSIVE
	)
};
static struct skeleton_format formats[] = {
	{
		.name     = "15 bpp RGB, le",
		.fourcc   = V4L2_PIX_FMT_RGB555,
		.depth    = 16,
		.pm       = 0x13 | 0x80,
	},{
		.name     = "16 bpp RGB, le",
		.fourcc   = V4L2_PIX_FMT_RGB565,
		.depth    = 16,
		.pm       = 0x10 | 0x80,
	},{
		.name     = "4:2:2 packed, YUYV",
		.fourcc   = V4L2_PIX_FMT_YUYV,
		.depth    = 16,
		.pm       = 0x00,
		.bswap    = 1,
		.yuv      = 1,
	},{
		.name     = "4:2:2 packed, UYVY",
		.fourcc   = V4L2_PIX_FMT_UYVY,
		.depth    = 16,
		.pm       = 0x00,
		.yuv      = 1,
	}
};


#define FORMATS ARRAY_SIZE(formats)

#define NORM_625_50			\
		.h_start       = 0,	\
		.h_stop        = 719,	\
		.video_v_start = 24,	\
		.video_v_stop  = 311,	\
		.vbi_v_start_0 = 7,	\
		.vbi_v_stop_0  = 22,	\
		.vbi_v_start_1 = 319,   \
		.src_timing    = 4

#define NORM_525_60			\
		.h_start       = 0,	\
		.h_stop        = 719,   \
		.video_v_start = 23,	\
		.video_v_stop  = 262,	\
		.vbi_v_start_0 = 10,	\
		.vbi_v_stop_0  = 21,	\
		.vbi_v_start_1 = 273,	\
		.src_timing    = 7

static struct skeleton_tvnorm tvnorms[] = {
	{
		.name          = "PAL", /* autodetect */
		.id            = V4L2_STD_PAL,
		NORM_625_50,

		.sync_control  = 0x18,
		.luma_control  = 0x40,
		.chroma_ctrl1  = 0x81,
		.chroma_gain   = 0x2a,
		.chroma_ctrl2  = 0x06,
		.vgate_misc    = 0x1c,

	},{
		.name          = "PAL-BG",
		.id            = V4L2_STD_PAL_BG,
		NORM_625_50,

		.sync_control  = 0x18,
		.luma_control  = 0x40,
		.chroma_ctrl1  = 0x81,
		.chroma_gain   = 0x2a,
		.chroma_ctrl2  = 0x06,
		.vgate_misc    = 0x1c,

	},{
		.name          = "PAL-I",
		.id            = V4L2_STD_PAL_I,
		NORM_625_50,

		.sync_control  = 0x18,
		.luma_control  = 0x40,
		.chroma_ctrl1  = 0x81,
		.chroma_gain   = 0x2a,
		.chroma_ctrl2  = 0x06,
		.vgate_misc    = 0x1c,

	},{
		.name          = "PAL-DK",
		.id            = V4L2_STD_PAL_DK,
		NORM_625_50,

		.sync_control  = 0x18,
		.luma_control  = 0x40,
		.chroma_ctrl1  = 0x81,
		.chroma_gain   = 0x2a,
		.chroma_ctrl2  = 0x06,
		.vgate_misc    = 0x1c,

	},{
		.name          = "NTSC",
		.id            = V4L2_STD_NTSC,
		NORM_525_60,

		.sync_control  = 0x59,
		.luma_control  = 0x40,
		.chroma_ctrl1  = 0x89,
		.chroma_gain   = 0x2a,
		.chroma_ctrl2  = 0x0e,
		.vgate_misc    = 0x18,

	},{
		.name          = "SECAM",
		.id            = V4L2_STD_SECAM,
		NORM_625_50,

		.sync_control  = 0x18,
		.luma_control  = 0x1b,
		.chroma_ctrl1  = 0xd1,
		.chroma_gain   = 0x80,
		.chroma_ctrl2  = 0x00,
		.vgate_misc    = 0x1c,

	},{
		.name          = "SECAM-DK",
		.id            = V4L2_STD_SECAM_DK,
		NORM_625_50,

		.sync_control  = 0x18,
		.luma_control  = 0x1b,
		.chroma_ctrl1  = 0xd1,
		.chroma_gain   = 0x80,
		.chroma_ctrl2  = 0x00,
		.vgate_misc    = 0x1c,

	},{
		.name          = "SECAM-L",
		.id            = V4L2_STD_SECAM_L,
		NORM_625_50,

		.sync_control  = 0x18,
		.luma_control  = 0x1b,
		.chroma_ctrl1  = 0xd1,
		.chroma_gain   = 0x80,
		.chroma_ctrl2  = 0x00,
		.vgate_misc    = 0x1c,

	},{
		.name          = "SECAM-Lc",
		.id            = V4L2_STD_SECAM_LC,
		NORM_625_50,

		.sync_control  = 0x18,
		.luma_control  = 0x1b,
		.chroma_ctrl1  = 0xd1,
		.chroma_gain   = 0x80,
		.chroma_ctrl2  = 0x00,
		.vgate_misc    = 0x1c,

	},{
		.name          = "PAL-M",
		.id            = V4L2_STD_PAL_M,
		NORM_525_60,

		.sync_control  = 0x59,
		.luma_control  = 0x40,
		.chroma_ctrl1  = 0xb9,
		.chroma_gain   = 0x2a,
		.chroma_ctrl2  = 0x0e,
		.vgate_misc    = 0x18,

	},{
		.name          = "PAL-Nc",
		.id            = V4L2_STD_PAL_Nc,
		NORM_625_50,

		.sync_control  = 0x18,
		.luma_control  = 0x40,
		.chroma_ctrl1  = 0xa1,
		.chroma_gain   = 0x2a,
		.chroma_ctrl2  = 0x06,
		.vgate_misc    = 0x1c,

	},{
		.name          = "PAL-60",
		.id            = V4L2_STD_PAL_60,

		.h_start       = 0,
		.h_stop        = 719,
		.video_v_start = 23,
		.video_v_stop  = 262,
		.vbi_v_start_0 = 10,
		.vbi_v_stop_0  = 21,
		.vbi_v_start_1 = 273,
		.src_timing    = 7,

		.sync_control  = 0x18,
		.luma_control  = 0x40,
		.chroma_ctrl1  = 0x81,
		.chroma_gain   = 0x2a,
		.chroma_ctrl2  = 0x06,
		.vgate_misc    = 0x1c,
	}
};
#define TVNORMS ARRAY_SIZE(tvnorms)
#define V4L2_CID_PRIVATE_INVERT      (V4L2_CID_PRIVATE_BASE + 0)
#define V4L2_CID_PRIVATE_Y_ODD       (V4L2_CID_PRIVATE_BASE + 1)
#define V4L2_CID_PRIVATE_Y_EVEN      (V4L2_CID_PRIVATE_BASE + 2)
#define V4L2_CID_PRIVATE_AUTOMUTE    (V4L2_CID_PRIVATE_BASE + 3)
#define V4L2_CID_PRIVATE_LASTP1      (V4L2_CID_PRIVATE_BASE + 4)

static const struct v4l2_queryctrl no_ctrl = {
	.name  = "42",
	.flags = V4L2_CTRL_FLAG_DISABLED,
};
static const struct v4l2_queryctrl video_ctrls[] = {
	/* --- video --- */
	{
		.id            = V4L2_CID_BRIGHTNESS,
		.name          = "Brightness",
		.minimum       = 0,
		.maximum       = 255,
		.step          = 1,
		.default_value = 125,
		.type          = V4L2_CTRL_TYPE_INTEGER,
	},{
		.id            = V4L2_CID_CONTRAST,
		.name          = "Contrast",
		.minimum       = 0,
		.maximum       = 200,  //127
		.step          = 1,
		.default_value = 96,
		.type          = V4L2_CTRL_TYPE_INTEGER,
	},{
		.id            = V4L2_CID_SATURATION,
		.name          = "Saturation",
		.minimum       = 0,
		.maximum       = 127,
		.step          = 1,
		.default_value = 64,
		.type          = V4L2_CTRL_TYPE_INTEGER,
	},{
		.id            = V4L2_CID_HUE,
		.name          = "Hue",
		.minimum       = -124,   //-128,
		.maximum       = 125,    // 127,
		.step          = 1,
		.default_value = 0,
		.type          = V4L2_CTRL_TYPE_INTEGER,
	},{
		.id            = V4L2_CID_HFLIP,
		.name          = "Mirror",
		.minimum       = 0,
		.maximum       = 1,
		.type          = V4L2_CTRL_TYPE_BOOLEAN,
	},
	/* --- audio --- */
	{
		.id            = V4L2_CID_AUDIO_MUTE,
		.name          = "Mute",
		.minimum       = 0,
		.maximum       = 1,
		.type          = V4L2_CTRL_TYPE_BOOLEAN,
	},{
		.id            = V4L2_CID_AUDIO_VOLUME,
		.name          = "Volume",
		.minimum       = -15,
		.maximum       = 15,
		.step          = 1,
		.default_value = 0,
		.type          = V4L2_CTRL_TYPE_INTEGER,
	},
	/* --- private --- */
	{
		.id            = V4L2_CID_PRIVATE_INVERT,
		.name          = "Invert",
		.minimum       = 0,
		.maximum       = 1,
		.type          = V4L2_CTRL_TYPE_BOOLEAN,
	},{
		.id            = V4L2_CID_PRIVATE_Y_ODD,
		.name          = "y offset odd field",
		.minimum       = 0,
		.maximum       = 128,
		.step          = 1,
		.default_value = 0,
		.type          = V4L2_CTRL_TYPE_INTEGER,
	},{
		.id            = V4L2_CID_PRIVATE_Y_EVEN,
		.name          = "y offset even field",
		.minimum       = 0,
		.maximum       = 128,
		.step          = 1,
		.default_value = 0,
		.type          = V4L2_CTRL_TYPE_INTEGER,
	},{
		.id            = V4L2_CID_PRIVATE_AUTOMUTE,
		.name          = "automute",
		.minimum       = 0,
		.maximum       = 1,
		.default_value = 1,
		.type          = V4L2_CTRL_TYPE_BOOLEAN,
	}
};
static const unsigned int CTRLS = ARRAY_SIZE(video_ctrls);

/*
void start_dma(struct skeleton *skel)
{ 
	u32 val;
	pci_read_config_word(skel->pdev,0x52,&val); 
	val = val | 0x1;
	pci_write_config_word(skel->pdev,0x52,val);
}

void stop_dma(struct skeleton *skel)
{
	u32 val;
	pci_read_config_word(skel->pdev,0x52,&val);
	val = val & ~0x1;
	pci_write_config_word(skel->pdev,0x52,val);
}
*/

void start_fb(struct skeleton *skel,int fb_id)
{
	u32 val;
	val = ioread32((u32 *)(skel->bmmio_4 + FB_BASE_ADDR[fb_id]) + FB_CONTROL_REG);
	val = val | 0x1;
	iowrite32(val,(u32 *)(skel->bmmio_4 + FB_BASE_ADDR[fb_id]) + FB_CONTROL_REG);
}

void stop_fb(struct skeleton *skel,int fb_id)
{
	u32 val;
	val = ioread32((u32 *)(skel->bmmio_4 + FB_BASE_ADDR[fb_id]) + FB_CONTROL_REG);
	val = val & ~0x1;
	iowrite32(val,(u32 *)(skel->bmmio_4 + FB_BASE_ADDR[fb_id]) + FB_CONTROL_REG);
}

u32 get_ddr_start_addr(struct skeleton *skel,int fb_id)
{
	return ioread32((u32 *)(skel->bmmio_4 + FB_BASE_ADDR[fb_id]) + FB_BUF_ADDR_REG);
}
u32 get_interrupt_status(struct skeleton *skel)
{
	return ioread32((u32 *)(skel->bmmio_4 + 0x80000000));
}
static int set_write_desc(struct dma_descriptor *wr_desc, u64 source, dma_addr_t dest, u32 ctl_dma_len, u32 id)
{
    wr_desc->src_addr_ldw = cpu_to_le32(source & 0xffffffffUL);
    wr_desc->src_addr_udw = cpu_to_le32((source >> 32));
    wr_desc->dest_addr_ldw = cpu_to_le32(dest & 0xffffffffUL);
    wr_desc->dest_addr_udw = cpu_to_le32((dest >> 32));
    wr_desc->ctl_dma_len = cpu_to_le32(ctl_dma_len | (id << 18));
    wr_desc->reserved[0] = cpu_to_le32(0x0);
    wr_desc->reserved[1] = cpu_to_le32(0x0);
    wr_desc->reserved[2] = cpu_to_le32(0x0);
    return 0;
}

static int set_lite_table_header(struct lite_dma_header *header)
{
    int i;
    for (i = 0; i < 128; i++)
        header->flags[i] = cpu_to_le32(0x0); 
    return 0;
}
static struct skeleton_format* format_by_fourcc(unsigned int fourcc)
{
	unsigned int i;
    printk("????????????????????? CHECK fourcc \n");

	for (i = 0; i < FORMATS; i++)
		if (formats[i].fourcc == fourcc )
			return formats+i;
	return NULL;
}
/*
 * Setup the constraints of the queue: besides setting the number of planes
 * per buffer and the size and allocation context of each plane, it also
 * checks if sufficient buffers have been allocated. Usually 3 is a good
 * minimum number: many DMA engines need a minimum of 2 buffers in the
 * queue and you need to have another available for userspace processing.
 */
 
//static int queue_setup(struct vb2_queue *vq, const struct v4l2_format *pfmt,
//					 unsigned int *nbuffers, unsigned int *nplanes,
//					 unsigned int sizes[], void *allocators[])
static int queue_setup(struct vb2_queue *vq,unsigned int *nbuffers, unsigned int *nplanes,
					 unsigned int sizes[], struct device *allocators[])
{
	struct skeleton_fh *skel_fh = vb2_get_drv_priv(vq);
	//struct skeleton *skel = skel_fh->skel;

	skel_fh->field = skel_fh->format.field;

	if (skel_fh->field == V4L2_FIELD_ALTERNATE) {
		/*
		 * You cannot use read() with FIELD_ALTERNATE since the field
		 * information (TOP/BOTTOM) cannot be passed back to the user.
		 */
		if (vb2_fileio_is_active(vq))
			return -EINVAL;
		skel_fh->field = V4L2_FIELD_TOP;
	}
//	allocators[0] = skel->alloc_ctx;
	
	if (vq->num_buffers + *nbuffers < 3)
		*nbuffers = 3 - vq->num_buffers;
	////for test///
	//*nbuffers = 2; 
	///////////
	if (*nplanes)
		return sizes[0] < skel_fh->format.sizeimage ? -EINVAL : 0;
	*nplanes = 1;
	sizes[0] = skel_fh->format.sizeimage;
	printk("sizes : %d \n",sizes[0]);
	return 0;
	
}

/*
 * Prepare the buffer for queueing to the DMA engine: check and set the
 * payload size.
 */
 
static int buffer_prepare(struct vb2_buffer *vb)
{
	struct skeleton_fh *skel_fh = vb2_get_drv_priv(vb->vb2_queue);
	struct skeleton *skel = skel_fh->skel;

	unsigned long size = skel_fh->format.sizeimage;

	if (vb2_plane_size(vb, 0) < size) {
		dev_err(&skel->pdev->dev, "buffer too small (%lu < %lu)\n",
			 vb2_plane_size(vb, 0), size);
		return -EINVAL;
	}

	vb2_set_plane_payload(vb, 0, size);
	return 0;
}


/*
 * Queue this buffer to the DMA engine.
 */
 
static void buffer_queue(struct vb2_buffer *vb)
{	 
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct skeleton_fh *skel_fh = vb2_get_drv_priv(vb->vb2_queue);
	struct skel_buffer *buf = container_of(vbuf, struct skel_buffer, vb);//to_skel_buffer(vb);
	unsigned long flags;
	//printk("buffer_queue -----\n");
	spin_lock_irqsave(&skel_fh->qlock,flags);
	list_add_tail(&buf->list, &skel_fh->buf_list);
	spin_unlock_irqrestore(&skel_fh->qlock,flags);
	/* TODO: Update any DMA pointers if necessary */
}
#if 0
static void return_all_buffers(struct skeleton_fh *skel_fh,
			       enum vb2_buffer_state state)
{
	struct skel_buffer *buf, *node;
	struct skeleton_fh *skel_fh_buf;
	struct skeleton *skel = skel_fh->skel;
	unsigned long flags;
	printk("return_all_buffers  ---- \n");

	/*spin_lock_irqsave(&skel_fh->qlock,flags);
	while (!list_empty(&skel_fh->buf_list)) {
		buf = list_entry(skel_fh->buf_list.next, struct skel_buffer, list);
		list_del(&buf->list);
		vb2_buffer_done(&buf->vb.vb2_buf, state);
	}
	
	spin_unlock_irqrestore(&skel_fh->qlock,flags);
	*/
	spin_lock_irqsave(&skel->tsklock,flags);
	
	while (!list_empty(&skel_fh->buf_list)) {
		buf = list_entry(skel_fh->buf_list.next, struct skel_buffer, list);
		list_del(&buf->list);
		vb2_buffer_done(&buf->vb.vb2_buf, state);
	}
	
	while (!list_empty(&skel->dma_task_list)) {
		buf = list_entry(&skel->dma_task_list.next, struct skel_buffer, list);
		list_del(&buf->list);
		vb2_buffer_done(&buf->vb.vb2_buf, state);
	}
	spin_unlock_irqrestore(&skel->tsklock,flags);
	
	printk("end return_all_buffers  ---- \n"); 
}
 #endif
/*
 * Start streaming. First check if the minimum number of buffers have been
 * queued. If not, then return -ENOBUFS and the vb2 framework will call
 * this function again the next time a buffer has been queued until enough
 * buffers are available to actually start the DMA engine.
 */  


static int start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct skeleton_fh *skel_fh = vb2_get_drv_priv(vq);
	struct skeleton * skel = skel_fh->skel;
	unsigned int flags;
	u32 val;
	int ret = 0;
 	printk("start_streaming ....\n");
	
	skel_fh->sequence = 0;
	skel->ch_status[skel_fh->vd_id] = 0x1;
	printk("skel_fh vd_id :%d\n",skel_fh->vd_id);
	playch_count ++;  
	spin_lock_irqsave(&chlock,flags);
	start_fb(skel,skel_fh->vd_id);
	clear_fb_int(skel,skel_fh->vd_id);
	release_curr_buff(skel,skel_fh->vd_id);
	spin_unlock_irqrestore(&chlock,flags);
	return ret;
}


/*
 * Stop the DMA engine. Any remaining buffers in the DMA queue are dequeued
 * and passed on to the vb2 framework marked as STATE_ERROR.
 */

static void stop_streaming(struct vb2_queue *vq)
{
	struct skeleton_fh *skel_fh = vb2_get_drv_priv(vq);
	struct skeleton_fh *fh_temp;
	struct skeleton *skel = skel_fh->skel;
	struct skel_buffer *buf ;
	unsigned int flags;
	
	/* TODO: stop DMA */ 
	printk("stop DMA  ---- \n"); 
	
	//
	/*	spin_lock_irqsave(&chlock,flags);
	stop_fb(skel,skel_fh->vd_id);
	skel->ch_status[skel_fh->vd_id] = 0x0;
	spin_unlock_irqrestore(&chlock,flags);

	spin_lock_irqsave(&skel->tsklock,flags);
	while (!list_empty(&skel->dma_task_list)) {
		buf = list_entry(skel->dma_task_list.next, struct skel_buffer, list);
		fh_temp = vb2_get_drv_priv(buf->vb.vb2_buf.vb2_queue);
		if(fh_temp->vd_id == skel_fh->vd_id)
		{
			list_del(&buf->list);
			vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);			
		}
	}
	spin_unlock_irqrestore(&skel->tsklock,flags);
	*/
	spin_lock_irqsave(&skel_fh->qlock,flags);
	stop_fb(skel,skel_fh->vd_id);
	skel->ch_status[skel_fh->vd_id] = 0x0;
	while (!list_empty(&skel_fh->buf_list)) {
		buf = list_entry(skel_fh->buf_list.next, struct skel_buffer, list);
		list_del(&buf->list);
		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
	}
	spin_unlock_irqrestore(&skel_fh->qlock,flags);	
	//

}

/*
 * The vb2 queue ops. Note that since q->lock is set we can use the standard
 * vb2_ops_wait_prepare/finish helper functions. If q->lock would be NULL,
 * then this driver would have to provide these ops.
 */
static struct vb2_ops skel_qops = {
	.queue_setup		= queue_setup,
	.buf_prepare		= buffer_prepare,
	.buf_queue		= buffer_queue,
	.start_streaming	= start_streaming,
	.stop_streaming		= stop_streaming,
	.wait_prepare		= vb2_ops_wait_prepare,
	.wait_finish		= vb2_ops_wait_finish,
};



/*
 * Required ioctl querycap. Note that the version field is prefilled with
 * the version of the kernel.
 */

static int skeleton_querycap(struct file *file, void *priv,
			     struct v4l2_capability *cap)
{
	struct skeleton_fh *skel_fh = file->private_data;
	struct skeleton *skel = skel_fh->skel;

printk("skeleton_querycap \n");
	strlcpy(cap->driver, KBUILD_MODNAME, sizeof(cap->driver));
	strlcpy(cap->card, "V4L2 PCI Skeleton", sizeof(cap->card));
	snprintf(cap->bus_info, sizeof(cap->bus_info), "PCI:%s",
		 pci_name(skel->pdev));
	cap->version = TW68_VERSION_CODE;
	cap->device_caps = V4L2_CAP_VIDEO_CAPTURE |
			V4L2_CAP_READWRITE | V4L2_CAP_STREAMING;
	cap->capabilities = cap->device_caps | V4L2_CAP_DEVICE_CAPS;
	/*cap->capabilities = 
		V4L2_CAP_VIDEO_CAPTURE |
		V4L2_CAP_VBI_CAPTURE |
		V4L2_CAP_READWRITE |  
		V4L2_CAP_STREAMING |
		V4L2_CAP_TUNER; */
	if (TW68_no_overlay <= 0)
		cap->capabilities |= V4L2_CAP_VIDEO_OVERLAY;

		printk(KERN_INFO " SKEL_s__querycap   GO   ! \n");
	return 0;
}



/*
 * Helper function to check and correct struct v4l2_pix_format. It's used
 * not only in VIDIOC_TRY/S_FMT, but also elsewhere if changes to the SDTV
 * standard, HDTV timings or the video input would require updating the
 * current format.
 */

static void skeleton_fill_pix_format(struct skeleton_fh *skel_fh,
				     struct v4l2_pix_format *pix)
{
	printk("skeleton_fill_pix_format \n");
	pix->pixelformat = VIDEO_FORMAT;
	if (skel_fh->input == 0) {
		/* S-Video input */
		printk(" S-Video input set large size\n");
		pix->width = 1920;//160;//720;1920  260  3840
		pix->height = 1080;//1080;//120;//(skel->std & V4L2_STD_525_60) ? 480 : 576;1080  121
		pix->field = V4L2_FIELD_INTERLACED ; //V4L2_FIELD_NONE;//V4L2_FIELD_INTERLACED;
		pix->colorspace = V4L2_COLORSPACE_SMPTE170M;//V4L2_COLORSPACE_SMPTE170M;
	} else { 
		/* HDMI input */
		pix->width = skel_fh->timings.bt.width;
		pix->height = skel_fh->timings.bt.height;
		if (skel_fh->timings.bt.interlaced) {
			pix->field = V4L2_FIELD_ALTERNATE;
			pix->height /= 2;
		} else {
			pix->field = V4L2_FIELD_NONE;
		}
		pix->colorspace = V4L2_COLORSPACE_REC709;
	} 

	/*  
	 * The YUYV format is four bytes for every two pixels, so bytesperline
	 * is width * 2.
	 */
	pix->bytesperline = pix->width * 2; //* 4;pix->width *2;//
	pix->sizeimage = pix->bytesperline * pix->height;
	printk("width :0x%x height :0x%x\n",pix->width,pix->height);
	pix->priv = 0;
}


static int skeleton_try_fmt_vid_cap(struct file *file, void *priv,
				    struct v4l2_format *f)
{
	struct skeleton_fh *skel_fh = file->private_data;
	struct v4l2_pix_format *pix = &f->fmt.pix;
	printk("skeleton_try_fmt_vid_cap \n");
	/*
	 * Due to historical reasons providing try_fmt with an unsupported
	 * pixelformat will return -EINVAL for video receivers. Webcam drivers,
	 * however, will silently correct the pixelformat. Some video capture
	 * applications rely on this behavior...
	 */
	if (pix->pixelformat != VIDEO_FORMAT)
		return -EINVAL;
	skeleton_fill_pix_format(skel_fh, pix);
	return 0;
}



static int skeleton_s_fmt_vid_cap(struct file *file, void *priv,
				  struct v4l2_format *f)
{
	struct skeleton_fh *skel_fh = file->private_data;
	int ret;
	printk("skeleton_s_fmt_vid_cap \n");
	ret = skeleton_try_fmt_vid_cap(file, priv, f);
	if (ret)
		return ret;

	/*
	 * It is not allowed to change the format while buffers for use with
	 * streaming have already been allocated.
	 */
	if (vb2_is_busy(&skel_fh->queue))
		return -EBUSY;

	/* TODO: change format */
	skel_fh->format = f->fmt.pix;
	return 0;
}

static int skeleton_g_fmt_vid_cap(struct file *file, void *priv,
				  struct v4l2_format *f)
{
	struct skeleton_fh *skel_fh = file->private_data;
printk("skeleton_g_fmt_vid_cap \n");
	f->fmt.pix = skel_fh->format;
	return 0; 
}

static int skeleton_enum_fmt_vid_cap(struct file *file, void *priv,
				     struct v4l2_fmtdesc *f)
{
	if (f->index != 0)
		return -EINVAL;
printk("skeleton_enum_fmt_vid_cap \n");
	f->pixelformat = VIDEO_FORMAT;
	return 0;
}

static int skeleton_s_std(struct file *file, void *priv, v4l2_std_id std)
{
	struct skeleton_fh *skel_fh = file->private_data;
	printk("skeleton_s_std \n");
	/* S_STD is not supported on the HDMI input */
	if (skel_fh->input)
		return -ENODATA;

	/*
	 * No change, so just return. Some applications call S_STD again after
	 * the buffers for streaming have been set up, so we have to allow for
	 * this behavior.
	 */
	if (std == skel_fh->std)
	{
		printk("std  :0x%x  no change just return \n",std);
		return 0;		
	}
	printk("std change  std:0x%x   skel->std :0x%x\n",std,skel_fh->std);

	/*
	 * Changing the standard implies a format change, which is not allowed
	 * while buffers for use with streaming have already been allocated.
	 */
	if (vb2_is_busy(&skel_fh->queue))
		return -EBUSY;

	/* TODO: handle changing std */

	skel_fh->std = std;

	/* Update the internal format */
	skeleton_fill_pix_format(skel_fh, &skel_fh->format);
	return 0;
}

static int skeleton_g_std(struct file *file, void *priv, v4l2_std_id *std)
{
	struct skeleton_fh *skel_fh = file->private_data;
	printk("skeleton_g_std \n");
	/* G_STD is not supported on the HDMI input */
	if (skel_fh->input)
		return -ENODATA;

	*std = skel_fh->std;
	return 0;
}

/*
 * Query the current standard as seen by the hardware. This function shall
 * never actually change the standard, it just detects and reports.
 * The framework will initially set *std to tvnorms (i.e. the set of
 * supported standards by this input), and this function should just AND
 * this value. If there is no signal, then *std should be set to 0.
 */
static int skeleton_querystd(struct file *file, void *priv, v4l2_std_id *std)
{
	struct skeleton_fh *skel_fh = file->private_data;
	printk("skeleton_querystd \n");
	/* QUERY_STD is not supported on the HDMI input */
	if (skel_fh->input)
		return -ENODATA;

#ifdef TODO
	/*
	 * Query currently seen standard. Initial value of *std is
	 * V4L2_STD_ALL. This function should look something like this:
	 */
	get_signal_info();
	if (no_signal) {
		*std = 0;
		return 0;
	}
	/* Use signal information to reduce the number of possible standards */
	if (signal_has_525_lines)
		*std &= V4L2_STD_525_60;
	else
		*std &= V4L2_STD_625_50;
#endif

	return 0;
}

static int skeleton_s_dv_timings(struct file *file, void *_fh,
				 struct v4l2_dv_timings *timings)
{
	struct skeleton_fh *skel_fh = file->private_data;

	/* S_DV_TIMINGS is not supported on the S-Video input */
	if(skel_fh->input == 0)
		return -ENODATA;

	/* Quick sanity check */
	if(!v4l2_valid_dv_timings(timings, &skel_timings_cap, NULL, NULL))
		return -EINVAL;

	/* Check if the timings are part of the CEA-861 timings. */
	if(!v4l2_find_dv_timings_cap(timings, &skel_timings_cap,
				      0, NULL, NULL))
		return -EINVAL;

	/* Return 0 if the new timings are the same as the current timings. */
	//if(v4l2_match_dv_timings(timings, &skel_fh->timings, 0))
	if(v4l2_match_dv_timings(timings, &skel_fh->timings, 0,false))
		return 0;

	/*
	 * Changing the timings implies a format change, which is not allowed
	 * while buffers for use with streaming have already been allocated.
	 */
	if (vb2_is_busy(&skel_fh->queue))
		return -EBUSY;

	/* TODO: Configure new timings */

	/* Save timings */
	skel_fh->timings = *timings;

	/* Update the internal format */
	skeleton_fill_pix_format(skel_fh, &skel_fh->format);
	return 0;
}

static int skeleton_g_dv_timings(struct file *file, void *_fh,
				 struct v4l2_dv_timings *timings)
{
	struct skeleton_fh *skel_fh = file->private_data;

	/* G_DV_TIMINGS is not supported on the S-Video input */
	if (skel_fh->input == 0)
		return -ENODATA;

	*timings = skel_fh->timings;
	return 0;
}

static int skeleton_enum_dv_timings(struct file *file, void *_fh,
				    struct v4l2_enum_dv_timings *timings)
{
	struct skeleton_fh *skel_fh = file->private_data;

	/* ENUM_DV_TIMINGS is not supported on the S-Video input */
	if (skel_fh->input == 0)
		return -ENODATA;

	return v4l2_enum_dv_timings_cap(timings, &skel_timings_cap,
					NULL, NULL);
}

/*
 * Query the current timings as seen by the hardware. This function shall
 * never actually change the timings, it just detects and reports.
 * If no signal is detected, then return -ENOLINK. If the hardware cannot
 * lock to the signal, then return -ENOLCK. If the signal is out of range
 * of the capabilities of the system (e.g., it is possible that the receiver
 * can lock but that the DMA engine it is connected to cannot handle
 * pixelclocks above a certain frequency), then -ERANGE is returned.
 */
static int skeleton_query_dv_timings(struct file *file, void *_fh,
				     struct v4l2_dv_timings *timings)
{
	struct skeleton_fh *skel_fh = file->private_data;

	/* QUERY_DV_TIMINGS is not supported on the S-Video input */
	if (skel_fh->input == 0)
		return -ENODATA;

#ifdef TODO
	/*
	 * Query currently seen timings. This function should look
	 * something like this:
	 */
	detect_timings();
	if (no_signal)
		return -ENOLINK;
	if (cannot_lock_to_signal)
		return -ENOLCK;
	if (signal_out_of_range_of_capabilities)
		return -ERANGE;

	/* Useful for debugging */
	v4l2_print_dv_timings(skel->v4l2_dev.name, "query_dv_timings:",
			timings, true);
#endif
	return 0;
}

static int skeleton_dv_timings_cap(struct file *file, void *fh,
				   struct v4l2_dv_timings_cap *cap)
{
	struct skeleton_fh *skel_fh = file->private_data;

	/* DV_TIMINGS_CAP is not supported on the S-Video input */
	if (skel_fh->input == 0)
		return -ENODATA;
	*cap = skel_timings_cap;
	return 0;
}

static int skeleton_enum_input(struct file *file, void *priv,
			       struct v4l2_input *i)
{
	if (i->index > 1)
		return -EINVAL;
printk("skeleton_enum_input\n");
	i->type = V4L2_INPUT_TYPE_CAMERA;
	if (i->index == 0) {
		i->std = SKEL_TVNORMS;
		strlcpy(i->name, "S-Video", sizeof(i->name));
		i->capabilities = V4L2_IN_CAP_STD;
	} else {
		i->std = 0;
		strlcpy(i->name, "HDMI", sizeof(i->name));
		i->capabilities = V4L2_IN_CAP_DV_TIMINGS;
	}
	return 0;
}

static int skeleton_s_input(struct file *file, void *priv, unsigned int i)
{
	struct skeleton_fh *skel_fh = file->private_data;
	struct skeleton *skel = skel_fh->skel;
	printk("skeleton_s_input\n");
	if (i > 1)
		return -EINVAL;

	/*
	 * Changing the input implies a format change, which is not allowed
	 * while buffers for use with streaming have already been allocated.
	 */
	if (vb2_is_busy(&skel_fh->queue))
		return -EBUSY;

	skel_fh->input = i;
	/*
	 * Update tvnorms. The tvnorms value is used by the core to implement
	 * VIDIOC_ENUMSTD so it has to be correct. If tvnorms == 0, then
	 * ENUMSTD will return -ENODATA.
	 */
	skel->vdev[skel_fh->vd_id]->tvnorms = i ? 0 : SKEL_TVNORMS;

	/* Update the internal format */
	skeleton_fill_pix_format(skel_fh, &skel_fh->format);
	return 0;
}

static int skeleton_g_input(struct file *file, void *priv, unsigned int *i)
{
	struct skeleton_fh *skel_fh = file->private_data;
printk("skeleton_g_input\n");
	*i = skel_fh->input;
	return 0;
}
static int skeleton_g_fbuf(struct file *file, void *f, struct v4l2_framebuffer *fb)
{
	struct skeleton_fh *skel_fh = file->private_data;
	//struct skeleton *skel = skel_fh->skel;


	printk( " ====== TW68__g_fbuf \n");

	*fb = skel_fh->ovbuf;
	fb->capability = V4L2_FBUF_CAP_LIST_CLIPPING;

	return 0;
}
static int skeleton_s_fbuf(struct file *file, void *f, const struct v4l2_framebuffer *fb)
{
	struct skeleton_fh *skel_fh = file->private_data;
	//struct skeleton *skel = skel_fh->skel;
	struct skeleton_format *fmt;

	printk( " ====== TW68__s_fbuf \n");

	if (!capable(CAP_SYS_ADMIN) &&
	   !capable(CAP_SYS_RAWIO))
		return -EPERM;

	/* check args */
	fmt = format_by_fourcc(fb->fmt.pixelformat);
	if (NULL == fmt)
		return -EINVAL;

	/* ok, accept it */
	skel_fh->ovbuf = *fb;
	skel_fh->ovfmt = fmt;
	if (0 == skel_fh->ovbuf.fmt.bytesperline)
		skel_fh->ovbuf.fmt.bytesperline =
			skel_fh->ovbuf.fmt.width*fmt->depth/8;
	return 0;
}



static int skeleton_reqbufs(struct file *file, void *priv,
					struct v4l2_requestbuffers *p)
{
	struct skeleton_fh *skel_fh = file->private_data;
	
	p->count = 5;//max_t(u32, 10, p->count);//REQ_BUFS_MIN
	return vb2_reqbufs(&skel_fh->queue, p);
}

static int skeleton_querybuf(struct file *file, void *priv,
					struct v4l2_buffer *b)
{
	struct skeleton_fh *skel_fh = file->private_data;
	return vb2_querybuf(&skel_fh->queue, b);
}

static int skeleton_qbuf(struct file *file, void *priv, struct v4l2_buffer *b)
{
	struct skeleton_fh *skel_fh = file->private_data;
	return vb2_qbuf(&skel_fh->queue, b);
}

static int skeleton_dqbuf(struct file *file, void *priv, struct v4l2_buffer *b)
{
	struct skeleton_fh *skel_fh = file->private_data;

	return vb2_dqbuf(&skel_fh->queue, b, file->f_flags & O_NONBLOCK);
}

static int skeleton_expbuf(struct file *file, void *priv, struct v4l2_exportbuffer *eb)
{
	struct skeleton_fh *skel_fh = file->private_data;

	return vb2_expbuf(&skel_fh->queue, eb);
}

static int skeleton_streamon(struct file *file, void *priv,
					enum v4l2_buf_type type)
{
	struct skeleton_fh *skel_fh = file->private_data;

	return vb2_streamon(&skel_fh->queue, type);
}


static int skeleton_streamoff(struct file *file, void *priv,
					enum v4l2_buf_type type)
{
	struct skeleton_fh *skel_fh = file->private_data;

	return vb2_streamoff(&skel_fh->queue, type);
}

static inline int calc_interval(u8 fps, u32 n, u32 d)
{
	if (!n || !d)
		return 1;
	if (d == fps)
		return n;
	n *= fps;
	return min(15U, n / d + (n % d >= (fps >> 1)));
}

static int skeleton_g_parm(struct file *file, void *priv,
		       struct v4l2_streamparm *sp)
{
	struct skeleton_fh *skel_fh = file->private_data;
	struct v4l2_captureparm *cp = &sp->parm.capture;
	printk("skeleton_g_parm  \n");
	cp->capability = V4L2_CAP_TIMEPERFRAME;
	cp->timeperframe.numerator = skel_fh->interval;
	cp->timeperframe.denominator = skel_fh->fps;
	cp->capturemode = 0;
	/* XXX: Shouldn't we be able to get/set this from videobuf? */
	cp->readbuffers = 2;
	return 0;
}

static int skeleton_s_parm(struct file *file, void *priv,
		       struct v4l2_streamparm *sp)
{
	struct skeleton_fh *skel_fh = file->private_data;
	struct v4l2_fract *t = &sp->parm.capture.timeperframe;
	u8 fps = skel_fh->fps;

	if (vb2_is_streaming(&skel_fh->queue))
		return -EBUSY;
	printk("skeleton_s_parm  \n");
	skel_fh->interval = calc_interval(fps, t->numerator, t->denominator);
	//solo_update_mode(skel_fh);
	return skeleton_g_parm(file, priv, sp);
}
/*
 * The set of all supported ioctls. Note that all the streaming ioctls
 * use the vb2 helper functions that take care of all the locking and
 * that also do ownership tracking (i.e. only the filehandle that requested
 * the buffers can call the streaming ioctls, all other filehandles will
 * receive -EBUSY if they attempt to call the same streaming ioctls).
 *
 * The last three ioctls also use standard helper functions: these implement
 * standard behavior for drivers with controls.
 */
const struct v4l2_ioctl_ops skel_ioctl_ops = {
	.vidioc_querycap = skeleton_querycap,/*查询设备支持的功能，只有VIDIOC_QUERY_CAP一个*/
	
	.vidioc_try_fmt_vid_cap = skeleton_try_fmt_vid_cap,/*与VIDIOC_S_FMT一样，但不会改变设备的状态*/
	.vidioc_s_fmt_vid_cap = skeleton_s_fmt_vid_cap,/*设置数据格式*/
	.vidioc_g_fmt_vid_cap = skeleton_g_fmt_vid_cap,/*获取数据格式*/
	.vidioc_enum_fmt_vid_cap = skeleton_enum_fmt_vid_cap, /*枚举设备所支持的所有数据格式*/

	.vidioc_g_std = skeleton_g_std,
	.vidioc_s_std = skeleton_s_std,
	.vidioc_querystd = skeleton_querystd,

	.vidioc_s_dv_timings = skeleton_s_dv_timings,
	.vidioc_g_dv_timings = skeleton_g_dv_timings,
	.vidioc_enum_dv_timings = skeleton_enum_dv_timings,
	.vidioc_query_dv_timings = skeleton_query_dv_timings,
	.vidioc_dv_timings_cap = skeleton_dv_timings_cap,

	.vidioc_enum_input = skeleton_enum_input,
	.vidioc_g_input = skeleton_g_input,
	.vidioc_s_input = skeleton_s_input,
	
	.vidioc_g_fbuf			= skeleton_g_fbuf,
	.vidioc_s_fbuf			= skeleton_s_fbuf, 
	
	//.vidioc_s_ctrl		 = vidioc_s_ctrl,
	//.vidioc_g_ctrl		 = vidioc_g_ctrl,
	
	.vidioc_s_parm			= skeleton_s_parm,
	.vidioc_g_parm			= skeleton_g_parm,
	
	.vidioc_reqbufs = skeleton_reqbufs,//vb2_ioctl_reqbufs,/*向设备请求视频缓冲区，即初始化视频缓冲区*/
	//.vidioc_create_bufs = vb2_ioctl_create_bufs,
	.vidioc_querybuf = skeleton_querybuf,//vb2_ioctl_querybuf,/*查询缓冲区的状态*/
	.vidioc_qbuf = skeleton_qbuf,//vb2_ioctl_qbuf,/*从设备获取一帧视频数据*/
	.vidioc_dqbuf = skeleton_dqbuf,//vb2_ioctl_dqbuf,
	.vidioc_expbuf = skeleton_expbuf,//vb2_ioctl_expbuf,
	.vidioc_streamon = skeleton_streamon,//vb2_ioctl_streamon,
	.vidioc_streamoff = skeleton_streamoff,//vb2_ioctl_streamoff,

	//.vidioc_log_status = v4l2_ctrl_log_status,
	//.vidioc_subscribe_event = v4l2_ctrl_subscribe_event,
	//.vidioc_unsubscribe_event = v4l2_event_unsubscribe,
};


static int video_open(struct file *file)
{
	int minor = video_devdata(file)->minor;
	struct skeleton *skel;
	struct skeleton_fh  *skel_fh;
	//unsigned int request =0;
	struct vb2_queue *q;
	int ret;
	int k;  //, used;
 	static const struct v4l2_dv_timings timings_def = V4L2_DV_BT_CEA_1280X720P60;
	
	mutex_lock(&skel_devlist_lock);
	list_for_each_entry(skel, &skel_devlist, devlist) {
		printk(KERN_INFO " @@@@@ video_open   minor=%d \n",minor);
		for (k=0; k< VIDEO_NUM; k++)    // 8 - 9
		{
			 printk("video_open search  skel->vdev[%d],  skel->vdev[k]->minor:%d  \n",k,skel->vdev[k]->minor );

			if (skel->vdev[k] && (skel->vdev[k]->minor == minor))
				goto found;
		}
	}
		printk(KERN_INFO "video_open  no real device found XXXX \n");

	mutex_unlock(&skel_devlist_lock);
	return -ENODEV;

found:
	mutex_unlock(&skel_devlist_lock);

	mutex_lock(&f_lock);

	printk(KERN_INFO "found video :video_open ID:%d\n", k);
	
	if (skel->video_opened[k])
	{
		mutex_unlock(&f_lock);
		printk(" EBUSY  video%d have already opened  \n", k);
		return -EBUSY;
	}

	skel->video_opened[k] = true;
	skel_fh = kzalloc(sizeof(*skel_fh),GFP_KERNEL);
	if (NULL == skel_fh) {
		printk(KERN_INFO"skel_fh kzalloc fail \n");
		mutex_unlock(&f_lock);
		return -ENOMEM;
	}

    printk(KERN_INFO "fh kzalloc  successful! \n");
	
	//if (k) {
		skel->skel_fh[k] = skel_fh;
		q = &skel_fh->queue;
		q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		q->io_modes = VB2_MMAP | VB2_DMABUF ;//| VB2_READ
		q->dev = &skel->pdev->dev;
		q->drv_priv = skel_fh;
		q->buf_struct_size = sizeof(struct skel_buffer);
		q->ops = &skel_qops;
		q->mem_ops = &vb2_dma_sg_memops;
		q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
		/*
		 * Assume that this DMA engine needs to have at least two buffers
		 * available before it can be started. The start_streaming() op
		 * won't be called until at least this many buffers are queued up.
		 */
		q->min_buffers_needed = 2;
		/*
		 * The serialization lock for the streaming ioctls. This is the same
		 * as the main serialization lock, but if some of the non-streaming
		 * ioctls could take a long time to execute, then you might want to
		 * have a different lock here to prevent VIDIOC_DQBUF from being
		 * blocked while waiting for another action to finish. This is
		 * generally not needed for PCI devices, but USB devices usually do
		 * want a separate lock here.
		 */
		q->lock = &skel_fh->fh_lock;
		/*
		 * Since this driver can only do 32-bit DMA we must make sure that
		 * the vb2 core will allocate the buffers in 32-bit DMA memory.
		 */
		q->gfp_flags = GFP_DMA32;
		ret = vb2_queue_init(q);
		if (ret)
		{
			mutex_unlock(&f_lock);
			kfree(skel_fh);
			printk("vb2 queue init fail,open fail \n");
		}
	//} else {
		//printk("\n No support for QF output! \n");
	//}
	INIT_LIST_HEAD(&skel_fh->buf_list);
	spin_lock_init(&skel_fh->qlock); 
	//INIT_WORK(&skel_fh->fb_work,work_handler);
	
	file->private_data = skel_fh;
	skel_fh->skel = skel;
	skel_fh->vd_id = k;
	skel_fh->type     = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	skel_fh->timings = timings_def;
	skel_fh->std = V4L2_STD_625_50;
	skel_fh->fps = 30;
	skel_fh->buf_in_task = 0;
	if (skel_fh->format.field == V4L2_FIELD_ALTERNATE) {
		if (skel_fh->field == V4L2_FIELD_BOTTOM)
			skel_fh->field = V4L2_FIELD_TOP;
		else if (skel_fh->field == V4L2_FIELD_TOP)
			skel_fh->field = V4L2_FIELD_BOTTOM;
	}
	skeleton_fill_pix_format(skel_fh, &skel_fh->format);

	//printk( KERN_INFO "open minor=%d  fh->DMA_nCH= 0x%X type=%s\n",minor, fh->DMA_nCH, v4l2_type_names[type]);
	printk(KERN_INFO "fh initialized uccessful! \n");
	mutex_unlock(&f_lock);
	return 0;
} 

static unsigned int video_poll(struct file *file, struct poll_table_struct *wait)
{ 
	struct skeleton_fh *skel_fh = file->private_data;
	struct skeleton *skel = skel_fh->skel;
	unsigned int ret = 0;

	//printk( "skel_fh %d %s\n",skel_fh->vd_id, __func__);
	if (V4L2_BUF_TYPE_VIDEO_CAPTURE != skel_fh->type)
		return POLLERR;

	mutex_lock(&f_lock);
	ret = vb2_poll(&skel_fh->queue, file, wait);
	mutex_unlock(&f_lock);
	return ret;
}

static int video_release(struct file *file)
{
	struct skeleton_fh *skel_fh = file->private_data;
	struct skeleton *skel = skel_fh->skel;
	printk("######video_release skel_fh id :%d \n",skel_fh->vd_id);
	
	mutex_lock(&f_lock);	
	vb2_queue_release(&skel_fh->queue); 
	file->private_data = NULL; 
	skel->skel_fh[skel_fh->vd_id] = NULL ;
	skel->video_opened[skel_fh->vd_id] = false;
	kfree(skel_fh);
	mutex_unlock(&f_lock);

	return 0;
}

static int video_mmap(struct file *file, struct vm_area_struct * vma)
{
	struct skeleton_fh *skel_fh = file->private_data;
	//struct skeleton *skel = skel_fh->skel;
	
	return vb2_mmap(&skel_fh->queue, vma);
}
/*
 * The set of file operations. Note that all these ops are standard core
 * helper functions.
 */
const struct v4l2_file_operations skel_fops = {
	.owner = THIS_MODULE,
	.open = video_open,// v4l2_fh_open,
	.release = video_release,//vb2_fop_release,
	.unlocked_ioctl = video_ioctl2,
	//.read = vb2_fop_read,
	.mmap = video_mmap,
	.poll = video_poll,//vb2_fop_poll,
};

int fill_write_desc(struct skeleton *skel,struct sg_table *vbuf,u32 framesize,u32 ddr_start_addr,u32 last_id)
{
	struct scatterlist *sg;
	int desc_count = 0;
	int j,temp,temp_x;
	u32 src_addr;
	dma_addr_t des_addr;
	u32 length;
	u32 desc_id;
	u32 size;
	int len;
	int end_flag = 0; 
	int k = 0;
	//printk("temp :0x%x ,size 0x%x \n",temp,skel->format.sizeimage );
	//temp = framesize / MAX_DMA_BYTES_NUM;   //！！！1024整数倍时！！！
	for_each_sg(vbuf->sgl, sg, vbuf->nents, j)
	{

		dma_addr_t dma; 
		u32* virt;
		dma = sg_dma_address(sg);
		len = sg_dma_len(sg);
		virt = phys_to_virt(dma);  
		memset(virt, 0, len);
		size = MIN(framesize,len);
		temp = size / MAX_DMA_BYTES_NUM; 
		temp_x = size % MAX_DMA_BYTES_NUM;
		//printk("for_each_sg :%d desc_count :%d temp :%d temp_x :%d len :%d  size :%d  framesize :%d \n",j,desc_count,temp,temp_x,len,size,framesize);
		while(end_flag == 0)
		{ 
			
			if(k < temp)  
			{
				src_addr = ddr_start_addr + k * MAX_DMA_BYTES_NUM;
				des_addr = dma + k * MAX_DMA_BYTES_NUM;
				length = MAX_DMA_BYTES_NUM >> 2;// 0x3FFFF;
				//length = 0x3FFFF;
				desc_id = last_id + desc_count ;//(last_id + desc_count + 1) % 128;
				//printk("1111111111111\n");
			}
			else if(temp_x != 0)
			{
				src_addr = ddr_start_addr + k * MAX_DMA_BYTES_NUM;
				des_addr = dma + k * MAX_DMA_BYTES_NUM;
				length = (size - (MAX_DMA_BYTES_NUM * k))>>2;
				desc_id = last_id + desc_count;//(last_id + desc_count + 1) % 128;
				//printk("22222222222222222\n");
				end_flag = 1;
			}
			else  
			{
				//printk("break\n"); 
				break;
			}
			//printk("src_addr :0x%x  des_addr :0x%x \n",src_addr,des_addr);
			//printk("src_addr :0x%x  des_addr :0x%x length :0x%x desc_id:0x%x \n",src_addr,des_addr,length,desc_id);
			set_write_desc(&skel->lite_table_wr_cpu_virt_addr->descriptors[desc_id], (u64)src_addr,(dma_addr_t)des_addr,length, desc_id);
			desc_count++;
			k++;
		}
		
		ddr_start_addr = ddr_start_addr + size;
		end_flag = 0;
		k = 0;
		framesize = framesize - size;
	}
	//printk("desc_count :%d  \n",desc_count);
	return desc_count;

}


int BFDMA_next(struct skeleton *skel,struct skel_buffer *buf)
{
    u32 last_id, write_127;
	u32 altera_dma_num_bytes;
	u32 altera_dma_descriptor_num;
	int ret;
	struct sg_table *vbuf;
	unsigned long flags;
	struct skeleton_fh *skel_fh;
	u32 ddr_start_addr ;
	

	skel_fh = vb2_get_drv_priv(buf->vb.vb2_buf.vb2_queue);
	if(skel->ch_status[skel_fh->vd_id] == 0x0)
	{
		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
		skel_fh->buf_in_task --;
		release_curr_buff(skel,skel_fh->vd_id);
		printk("ch%d status close\n",skel_fh->vd_id);
		return 1;
	}
	skel->curr_buf = buf; 
	skel->curr_fh_id = skel_fh->vd_id;

	ddr_start_addr = get_ddr_start_addr(skel,skel_fh->vd_id); //skel->ddr_start_addr;
	////////////////test///////////////
/*	if(ddr_start_addr == 0x0)
		ddr_start_addr = 0x7e9000;
	else if(ddr_start_addr == 0x3F4800)
		ddr_start_addr = 0x0;
	else if(ddr_start_addr == 0x7e9000)
		ddr_start_addr =0x3F4800;*/
	/////////////////////////////////
	vbuf = vb2_dma_sg_plane_desc(&buf->vb.vb2_buf,0);
	if (vb2_plane_size(&buf->vb.vb2_buf, 0) < skel_fh->format.sizeimage)
	{
		printk("vb2 size are less than image size! \n");
		skel_fh->buf_in_task --;
		return 0;
	}
	
	vb2_set_plane_payload(&buf->vb.vb2_buf, 0, skel_fh->format.sizeimage);
	
	ret = dma_map_sg(&skel->pdev->dev, vbuf->sgl, vbuf->nents, DMA_FROM_DEVICE);
	if (!ret)
	{
		printk("dma_map_sg  fail \n");
		skel_fh->buf_in_task --;
		return 0;
	}
	write_127 = 0;
	altera_dma_num_bytes = skel_fh->format.sizeimage;
	last_id = ioread32((u32 *)(skel->bmmio+DESC_CTRLLER_BASE+ALTERA_LITE_DMA_WR_LAST_PTR));
	//printk("last_id :0x%x \n",last_id);
	
	//memset(rp_wr_buffer_virt_addr, 0, bk_ptr->dma_status.altera_dma_num_dwords*4);
	set_lite_table_header((struct lite_dma_header *)skel->lite_table_wr_cpu_virt_addr);
	wmb();
	iowrite32 ((dma_addr_t)skel->lite_table_wr_bus_addr, skel->bmmio+DESC_CTRLLER_BASE+ALTERA_LITE_DMA_WR_RC_LOW_SRC_ADDR);
	iowrite32 (((dma_addr_t)skel->lite_table_wr_bus_addr)>>32, skel->bmmio+DESC_CTRLLER_BASE+ALTERA_LITE_DMA_WR_RC_HIGH_SRC_ADDR);
	
	if(last_id == 0xFF){
		//printk("set table dest addr\n");//WR_CTRL_BUF_BASE_LOW  WR_CTRL_BUF_BASE_HI
		iowrite32 (WR_CTRL_BUF_BASE_LOW, skel->bmmio+DESC_CTRLLER_BASE+ALTERA_LITE_DMA_WR_CTLR_LOW_DEST_ADDR);
		iowrite32 (WR_CTRL_BUF_BASE_HI, skel->bmmio+DESC_CTRLLER_BASE+ALTERA_LITE_DMA_WR_CTRL_HIGH_DEST_ADDR);
	} 
	wmb();  
	if(last_id == 0xFF) last_id = 127;
	last_id = 0;
	//printk("last_id :0x%x \n",last_id);
	altera_dma_descriptor_num = fill_write_desc(skel,vbuf,altera_dma_num_bytes,ddr_start_addr,last_id);
	last_id = last_id + altera_dma_descriptor_num;
		/*if(i_test == 0)
			i_test = 1;
		else if (i_test == 1)
			i_test = 2;
		else if (i_test == 2) 
			i_test = 3; 
		else if (i_test == 3)
			i_test = 0;*/
	//i_test = 0;
	//iowrite32(i_test , skel->bmmio+DESC_CTRLLER_BASE+0x114);
	iowrite32(last_id - 1 , skel->bmmio+DESC_CTRLLER_BASE+0x114);
	wmb();
	//iowrite32 (i_test , skel->bmmio+DESC_CTRLLER_BASE+ALTERA_LITE_DMA_WR_LAST_PTR);
	iowrite32 (last_id - 1 , skel->bmmio+DESC_CTRLLER_BASE+ALTERA_LITE_DMA_WR_LAST_PTR);
	have_dma = true;
	return 1;
}
