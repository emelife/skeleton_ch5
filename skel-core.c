/*
 * This is a V4L2 PCI Skeleton Driver. It gives an initial skeleton source
 * for use with other PCI drivers.
 *
 * This skeleton PCI driver assumes that the card has an S-Video connector as
 * input 0 and an HDMI connector as input 1.
 *
 * Copyright 2014 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 *
 * This program is free software; you may redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/types.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>
#include <linux/time.h>
#include <linux/list.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kmod.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/kthread.h>
#include <linux/interrupt.h>
#include <linux/videodev2.h>
#include <linux/v4l2-dv-timings.h>
#include <linux/dma-mapping.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <media/v4l2-device.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-dv-timings.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-event.h>
#include <linux/workqueue.h>
#include <media/videobuf2-dma-contig.h>
#include "altera_dma.h"
#include "skel.h"

MODULE_DESCRIPTION("V4L2 PCI Skeleton Driver");
MODULE_AUTHOR("Emel Liu");
MODULE_LICENSE("GPL v2");
#define SKEL_VERSION_CODE KERNEL_VERSION(3, 18, 20)
extern struct list_head skel_devlist;
extern struct mutex skel_devlist_lock;
extern int TW68_no_overlay;
//LIST_HEAD(skel_devlist);
extern int first_time;
int num_irq;
bool have_dma = false;
u32 dma_irq_count = 0;
u32 fb_irq_count = 0;
u32 frame_count = 0;
spinlock_t irqlock;
spinlock_t chlock;
struct timeval tv1;
struct timeval tv2;
int task_list_count = 0;
static struct workqueue_struct *fb_workqueue=NULL; 
struct task_struct *dma_task = NULL;
//static unsigned int video_nr[] = {[0 ... (TW68_MAXBOARDS - 1)] = UNSET };
static const struct pci_device_id skeleton_pci_tbl[] = {
	{ PCI_DEVICE(ALTERA_DMA_VID, ALTERA_DMA_DID) },
	{ 0, }
};

u32 FB_BASE_ADDR[VIDEO_NUM]={0x80005000 , 0x80005040 , 0x80005080 , 0x800050c0 , 0x80005100};  //5 channels
//u32 FB_BASE_ADDR[VIDEO_NUM]={0x80005000};                                                        //1 channel
//hello world
MODULE_DEVICE_TABLE(pci, skeleton_pci_tbl);
void release_curr_buff(struct skeleton *skel,int fb_id)
{
	u32 val;
	val = ioread32((u32 *)(skel->bmmio_4 + FB_BASE_ADDR[fb_id]) + FB_MISC_REG);
	val = val | 0x1;
	iowrite32(val,(u32 *)(skel->bmmio_4 + FB_BASE_ADDR[fb_id]) + FB_MISC_REG);
}

void clear_fb_int(struct skeleton *skel,int fb_id)
{
	u32 val;
	val = ioread32((u32 *)(skel->bmmio_4 + FB_BASE_ADDR[fb_id]) + FB_INT_CLR_REG);
	val = val | 0x1;
	iowrite32(val,(u32 *)(skel->bmmio_4 + FB_BASE_ADDR[fb_id]) + FB_INT_CLR_REG);
}

void set_irq_mask(struct skeleton *skel , int mask)
{
	iowrite32(mask,(u32 *)(skel->bmmio_4 + IRQ_BASE ) + IRQ_MASK);
}

u32 get_irq_status(struct skeleton *skel)
{
	return ioread32((u32 *)skel->bmmio_4 + IRQ_STATUS);
}

/* The control handler. */
static int skeleton_s_ctrl(struct v4l2_ctrl *ctrl)
{
	/*struct skeleton *skel =
		container_of(ctrl->handler, struct skeleton, ctrl_handler);*/
	printk("skeleton_s_ctrl\n");
	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		printk("TODO: set brightness to ctrl->val \n");
		break;
	case V4L2_CID_CONTRAST:
	printk(" TODO: set contrast to ctrl->val\n");
		/* TODO: set contrast to ctrl->val */
		break;
	case V4L2_CID_SATURATION:
	printk("TODO: set saturation to ctrl->val \n");
		/* TODO: set saturation to ctrl->val */
		break;
	case V4L2_CID_HUE:
	printk(" TODO: set hue to ctrl->val\n");
		/* TODO: set hue to ctrl->val */
		break;
	default: 
		return -EINVAL;
	}
	return 0;
}

static const struct v4l2_ctrl_ops skel_ctrl_ops = {
	.s_ctrl = skeleton_s_ctrl,
};

int dma_function(void *data){
	struct skeleton * skel = (struct skeleton *)data;
	struct skel_buffer *buf ;
	unsigned long flags;
	printk("come into dma_func \n");
	while(!kthread_should_stop())
	{
		set_current_state(TASK_INTERRUPTIBLE);
		spin_lock_irqsave(&skel->tsklock,flags);
		if(list_empty(&skel->dma_task_list)|| have_dma)
		{
			spin_unlock_irqrestore(&skel->tsklock,flags);
			schedule();
			//usleep_range(20,20);
			if(kthread_should_stop())
				break;
			
		}
		else
		{
			set_current_state(TASK_RUNNING); 
			buf = list_entry(skel->dma_task_list.next, struct skel_buffer, list);
			list_del(&buf->list);
			spin_unlock_irqrestore(&skel->tsklock,flags);
			BFDMA_next(skel,buf);
			//task_list_count --;
			//if(task_list_count > 2)
			//printk("#####task_list_count :%d \n ",task_list_count);
			
		}
	}
	set_current_state(TASK_RUNNING);
	return 0;
}


/*
 * Interrupt handler: typically interrupts happen after a new frame has been
 * captured. It is the job of the handler to remove the new frame from the
 * internal list and give it back to the vb2 framework, updating the sequence
 * counter, field and timestamp at the same time.
 */
static irqreturn_t dma_irqhandler(int irq, void *dev_id)
{
	struct skeleton * skel = (struct skeleton *)dev_id; 
	struct skel_buffer * new_buf;
	struct skeleton_fh *skel_fh;
	struct vb2_v4l2_buffer *vbuf;
	//struct skel_buffer *buf ;
	unsigned long flags;
	//printk("dma interrupt id %d ....\n",skel->curr_fh_id); 
	
	
	if(skel->ch_status[skel->curr_fh_id] == 0x0)
	{
		have_dma = false;
		skel_fh->buf_in_task --;
		wake_up_process(dma_task);
		spin_unlock_irqrestore(&chlock,flags);
		vb2_buffer_done(&skel->curr_buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
		printk("channel %d status close\n",skel->curr_fh_id);
		return IRQ_HANDLED;
	}
	release_curr_buff(skel,skel->curr_fh_id);
	spin_lock_irqsave(&chlock,flags);
	
	skel_fh = skel->skel_fh[skel->curr_fh_id];
	new_buf = skel->curr_buf;
	have_dma = false;	
	
	vbuf = &new_buf->vb;
	vbuf->sequence = skel_fh->sequence ++;
    new_buf->vb.field = V4L2_FIELD_INTERLACED;
	new_buf->vb.vb2_buf.timestamp = ktime_get_ns();
	vb2_buffer_done(&new_buf->vb.vb2_buf, VB2_BUF_STATE_DONE);

	wake_up_process(dma_task);
	skel_fh->buf_in_task --;
	spin_unlock_irqrestore(&chlock,flags);
	
	return IRQ_HANDLED;
}
 
static irqreturn_t fb_irqhandler(int irq, void *dev_id)
{
	struct skeleton * skel = (struct skeleton *)dev_id;	
	int num;
	unsigned long flags;
	struct skeleton_fh * skel_fh ;
	struct skel_buffer *buf;
	num = irq - skel->irq - 1;
	skel_fh = skel->skel_fh[num];
	//printk("fb interrupt ....  num :%d\n",num);
	
	clear_fb_int(skel,num);
	if(skel->ch_status[num] == 0x0)
	{
		printk("fb_irqhandler ch%d status close\n",num);
		return IRQ_HANDLED;
	}

	spin_lock_irqsave(&skel_fh->qlock,flags);
	if (!list_empty(&skel_fh->buf_list)){
		if(skel_fh->buf_in_task == 1)
		{
			spin_unlock_irqrestore(&skel_fh->qlock,flags);
			wake_up_process(dma_task);			
			release_curr_buff(skel,num);
			//clear_fb_int(skel,num);
			skel_fh->buf_in_task --;
			printk("ch:%d too much buffer in  task_list : %d  \n",skel_fh->vd_id,task_list_count);
			return IRQ_HANDLED;			
		}
		else
		{
			buf = list_entry(skel_fh->buf_list.next, struct skel_buffer, list);
			list_del(&buf->list);		
			skel_fh->buf_in_task ++;
			spin_unlock_irqrestore(&skel_fh->qlock,flags);	
		}
	} else {
		spin_unlock_irqrestore(&skel_fh->qlock,flags);
		wake_up_process(dma_task);
		release_curr_buff(skel,num);
		//clear_fb_int(skel,num);
		skel_fh->buf_in_task --;
		printk("channel :%d  no free buffer \n",skel_fh->vd_id);
		return IRQ_HANDLED;
	}
	
	spin_lock_irqsave(&skel->tsklock,flags);
	list_add_tail(&buf->list, &skel->dma_task_list);
	spin_unlock_irqrestore(&skel->tsklock,flags);
	wake_up_process(dma_task);
	return IRQ_HANDLED;
}

static void skel_unregister_video(struct skeleton *skel)
{
    int k;
	struct video_device *vdev;
    for( k =0; k< VIDEO_NUM; k++ )   /// 0 + 4
    {
		vdev = skel->vdev[k];
		if (vdev)
		{
			if (-1 != vdev->minor)
			{
				printk(KERN_INFO "video_unregister_device(vdev %d ) \n", vdev->minor );
				video_unregister_device(vdev);
				skel->vdev[k] = NULL;
				
			}
		}
    }
}

int vdev_init(struct skeleton *skel)
{
	int i;
	int ret;
	//struct video_device *vdev_temp = &skel_video_template;
	//char modname[32];
	struct video_device *vdev[VIDEO_NUM];
	
	for(i = 0;i< VIDEO_NUM ;i++)
	{
		/* Initialize the video_device structure */
		vdev[i] = video_device_alloc();
		snprintf(vdev[i]->name, sizeof(vdev[i]->name), "%s-%s%d ",skel->name, "video",i);
		/*
		 * There is nothing to clean up, so release is set to an empty release
		 * function. The release callback must be non-NULL.
		 */
		vdev[i]->release = video_device_release;
		vdev[i]->fops = &skel_fops,
		vdev[i]->ioctl_ops = &skel_ioctl_ops,
		vdev[i]->minor = -1;
		/*
		 * The main serialization lock. All ioctls are serialized by this
		 * lock. Exception: if q->lock is set, then the streaming ioctls
		 * are serialized by that separate lock.
		 */
		vdev[i]->lock = &skel->lock;;
		vdev[i]->v4l2_dev = &skel->v4l2_dev;
		/* Supported SDTV standards, if any */
		vdev[i]->tvnorms = SKEL_TVNORMS;
		//video_set_drvdata(vdev, skel);
		skel->vdev[i] = vdev[i];
		ret = video_register_device(skel->vdev[i], VFL_TYPE_GRABBER, -1);//video_nr[skel->nr]
		if (ret < 0) {
			video_device_release(skel->vdev[i]);
			skel->vdev[i] = NULL;
			printk( "video_register_device failed: %d\n", ret);
			return ret;
		}
		skel->video_opened[i] = false;
		//skel->ch_status[i] = 0x0;
		printk("video_register_device success minor :%d  ret :%d \n",skel->vdev[i]->minor,ret);
	}
	return 0;

}
/*
 * The initial setup of this device instance. Note that the initial state of
 * the driver should be complete. So the initial format, standard, timings
 * and video input should all be initialized to some reasonable value.
 */
static int skeleton_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	/* The initial timings are chosen to be 720p60. */

	struct skeleton *skel; 

	struct v4l2_ctrl_handler *hdl;
	//struct vb2_queue *q;
	int ret;
	int i;

	/*if (skel_devcount == TW68_MAXBOARDS)
	   return -ENOMEM;*/

	/* Enable PCI */
	ret = pci_enable_device(pdev);
	if (ret)
		return ret;

	if(pci_msi_enabled())
	{
		num_irq = pci_enable_msi_range(pdev,2,VIDEO_NUM + 1);
		if(num_irq < 0)	{		
			printk("pci_enable_msi_range enable error!\n");		
			goto  pci_disable_device;
		}else if(num_irq < VIDEO_NUM +1)
		{
			printk("can't get enough irq num :%d!\n",num_irq);
			
		}else
			printk("get %d irqs successfully!\n",num_irq);	
		
	}else
	{  
		printk("msi can't enable \n");
		goto  pci_disable_device;
	}

	
	ret = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
	if (ret) {
		dev_err(&pdev->dev, "no suitable DMA available.\n");
		goto pci_disable_msi;
	}

	/* Allocate a new instance */
	skel = kzalloc(sizeof(struct skeleton),GFP_KERNEL);//devm_kzalloc(&pdev->dev, sizeof(struct skeleton), GFP_KERNEL);
	if (!skel)
	{
		printk("devm_kzalloc error \n");
		goto pci_disable_msi;
	}
	ret = v4l2_device_register(&pdev->dev, &skel->v4l2_dev);
	if (ret)
		goto free_skel;

	//skel->nr = skel_devcount;
	skel->irq = pdev->irq;
	sprintf(skel->name,"skel%s","_emel");
		/* get mmio */
	if (!request_mem_region(pci_resource_start(pdev,0),
				pci_resource_len(pdev,0),
				skel->name)) {
		ret = -EBUSY;
		printk(KERN_ERR "%s: can't get MMIO memory @ 0x%llx\n",
		       skel->name,(unsigned long long)pci_resource_start(pdev,0));
		goto v4l2_device_unregister;
	}
	
	if (!request_mem_region(pci_resource_start(pdev,4),
				pci_resource_len(pdev,4),
				skel->name)) {
		ret = -EBUSY;
		printk(KERN_ERR "%s: can't get MMIO memory @ 0x%llx\n",
		       skel->name,(unsigned long long)pci_resource_start(pdev,0));
		goto release_mem_region_0;
	}

	//bar 0
	printk("bar 0 start : 0x%x   length : 0x%x \n",pci_resource_start(pdev, 0),pci_resource_len(pdev, 0));
	skel->lmmio = ioremap_nocache(pci_resource_start(pdev, 0),  pci_resource_len(pdev, 0));
	skel->bmmio = (__u8 __iomem *)skel->lmmio;
	if (NULL == skel->lmmio) {
		ret = -EIO;
		printk(KERN_ERR "%s: can't ioremap() MMIO memory\n",
		       skel->name);
		goto release_mem_region_4;
	}
	 
	//bar4
	printk("bar 4 start : 0x%x   length : 0x%x \n",pci_resource_start(pdev, 4),pci_resource_len(pdev, 4));
	skel->lmmio_4 = ioremap_nocache(pci_resource_start(pdev, 4),  pci_resource_len(pdev, 4));
	skel->bmmio_4 = (__u8 __iomem *)skel->lmmio_4;
	if (NULL == skel->lmmio_4) {
		ret = -EIO;
		printk(KERN_ERR "%s: can't ioremap() bar_4 MMIO memory\n", 
		       skel->name);
		goto iounmap_0;
	}
    skel->lite_table_wr_cpu_virt_addr = ((struct lite_dma_desc_table *)pci_alloc_consistent(pdev, sizeof(struct lite_dma_desc_table), &skel->lite_table_wr_bus_addr));
    if (!skel->lite_table_wr_bus_addr || !skel->lite_table_wr_cpu_virt_addr) {
        ret = -ENOMEM;
        goto iounmap_4; 
    }

	/* Allocate the interrupt */

	ret = request_irq(pdev->irq, dma_irqhandler,  IRQF_SHARED , "dma_irqhandler", skel);  ///   IRQF_SHARED | IRQF_DISABLED	
	if (ret < 0) {		
		printk(KERN_ERR "%s: can't get IRQ %d\n",skel->name,pdev->irq);		
		goto pci_free_consistent;	 
	}		
	for(i=1;i<num_irq;i++)
	{		
		ret = request_irq(pdev->irq+i, fb_irqhandler,  IRQF_SHARED , "fb_irqhandler", skel);  ///   IRQF_SHARED | IRQF_DISABLED	
		if (ret < 0) {		
			printk(KERN_ERR "%s: can't get IRQ %d\n",skel->name,pdev->irq+i);		
			goto pci_free_consistent;	 
		}
	}
	printk("get IRQ %s \n",skel->name);
	skel->pdev = pdev;
	
	set_irq_mask(skel,0xff);
	pci_set_master(pdev);
	pci_set_drvdata(pdev, skel);
	
	/*skel->alloc_ctx = vb2_dma_contig_init_ctx(&pdev->dev);
	if (IS_ERR(skel->alloc_ctx)) {
		ret = PTR_ERR(skel->alloc_ctx);
		goto fail5;
	}*/
	/* Fill in the initial format-related settings */


	/* Initialize the top-level structure */
/* Add the controls */

	
	hdl = &skel->ctrl_handler;
	v4l2_ctrl_handler_init(hdl, 4);
	v4l2_ctrl_new_std(hdl, &skel_ctrl_ops, V4L2_CID_BRIGHTNESS, 0, 255, 1, 127);
	v4l2_ctrl_new_std(hdl, &skel_ctrl_ops, V4L2_CID_CONTRAST, 0, 255, 1, 16);
	v4l2_ctrl_new_std(hdl, &skel_ctrl_ops, V4L2_CID_SATURATION, 0, 255, 1, 127);
	v4l2_ctrl_new_std(hdl, &skel_ctrl_ops, V4L2_CID_HUE, -128, 127, 1, 0);
	if (hdl->error) {
		ret = hdl->error;
		goto free_irq;
	}
	skel->v4l2_dev.ctrl_handler = hdl;
	
	mutex_init(&skel->lock);
	mutex_init(&skel_devlist_lock);
	list_add_tail(&skel->devlist, &skel_devlist);
	if(vdev_init(skel))
		goto free_video_device;
	dev_info(&pdev->dev, "V4L2 PCI Skeleton Driver loaded\n");
	TW68_no_overlay = -1;
	/*
	fb_workqueue = create_singlethread_workqueue("fb_workqueue");//创建一个单线程的工作队列
	if (!fb_workqueue)  
		goto wqerro; 
	*/
	INIT_LIST_HEAD(&skel->dma_task_list);
	spin_lock_init(&skel->tsklock);
	spin_lock_init(&skel->currlock);
	spin_lock_init(&irqlock);
	spin_lock_init(&chlock);
	dma_task = kthread_run(dma_function, (void *)skel, "dma_task");  
    if (IS_ERR(dma_task)) {  
        printk(KERN_INFO "create kthread failed!\n");
		dma_task = NULL ;
		goto wqerro;   
    }  
    else {  
        printk(KERN_INFO "create ktrhead ok!\n");  
    }
	return 0;
wqerro:
free_video_device:
	skel_unregister_video(skel);
	v4l2_ctrl_handler_free(&skel->ctrl_handler);
free_irq:
	for(i=0;i<num_irq;i++) 
	{
		free_irq(pdev->irq+i, skel); 		 
	}
pci_free_consistent:
	pci_free_consistent(pdev, sizeof(struct lite_dma_desc_table), skel->lite_table_wr_cpu_virt_addr, skel->lite_table_wr_bus_addr);
iounmap_4:
	iounmap(skel->lmmio_4); 
iounmap_0:	
	iounmap(skel->lmmio);	
release_mem_region_4:	
	release_mem_region(pci_resource_start(pdev,4), pci_resource_len(pdev,4));
release_mem_region_0:
	release_mem_region(pci_resource_start(pdev,0), pci_resource_len(pdev,0));
v4l2_device_unregister:
	v4l2_device_unregister(&skel->v4l2_dev);
free_skel:
	kfree(skel);
pci_disable_msi:
	pci_disable_msi(pdev);
pci_disable_device:
	pci_disable_device(pdev);
	return ret;
}

static void skeleton_remove(struct pci_dev *pdev)
{
	struct skeleton *skel = pci_get_drvdata(pdev);
	int i;
	if(dma_task){
		kthread_stop(dma_task);
		dma_task = NULL;
	}
	skel_unregister_video(skel);
	v4l2_ctrl_handler_free(&skel->ctrl_handler);
	for(i=0;i<num_irq;i++) 
	{
		free_irq(pdev->irq+i, skel); 		 
	}
	pci_free_consistent(pdev, sizeof(struct lite_dma_desc_table), skel->lite_table_wr_cpu_virt_addr, skel->lite_table_wr_bus_addr);
	iounmap(skel->lmmio_4); 
	iounmap(skel->lmmio);
	release_mem_region(pci_resource_start(pdev,4), pci_resource_len(pdev,4));
	release_mem_region(pci_resource_start(pdev,0), pci_resource_len(pdev,0));
	v4l2_device_unregister(&skel->v4l2_dev);

	kfree(skel);
	pci_disable_msi(pdev);
	pci_disable_device(pdev);

}

static struct pci_driver skeleton_driver = {
	.name = KBUILD_MODNAME,
	.probe = skeleton_probe,
	.remove = skeleton_remove,
	.id_table = skeleton_pci_tbl,
};

static int __init skel_init(void)
{
	
	INIT_LIST_HEAD(&skel_devlist);
	printk(KERN_INFO "SKEL: v4l2 driver version %d.%d.%d loaded\n", 
			SKEL_VERSION_CODE >>16, (SKEL_VERSION_CODE >>8)&0xFF, SKEL_VERSION_CODE &0xFF);

	pci_register_driver(&skeleton_driver);
	return 0;
}

static void __exit skel_fini(void)
{
	pci_unregister_driver(&skeleton_driver);
	printk(KERN_INFO "SKEL: v4l2 driver version %d.%d.%d removed\n", 
			SKEL_VERSION_CODE >>16, (SKEL_VERSION_CODE >>8)&0xFF, SKEL_VERSION_CODE &0xFF);

}

module_init(skel_init);
module_exit(skel_fini);
