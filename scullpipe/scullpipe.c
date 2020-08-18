#include <linux/init.h>
#include <linux/module.h>

#include <linux/kernel.h>	/* printk(), min() */
#include <linux/slab.h>		/* kmalloc() */
#include <linux/fs.h>		/* everything... */
#include <linux/proc_fs.h>
#include <linux/errno.h>	/* error codes */
#include <linux/types.h>	/* size_t */
#include <linux/fcntl.h>
#include <linux/poll.h>
#include <linux/cdev.h>
#include <asm/uaccess.h>
#include <linux/version.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0)
#include <linux/sched/signal.h>
#endif


#define DEV_NAME "scull_pipe"

/*
 * Macros to help debugging
 */

#undef PDEBUG             /* undef it, just in case */
#define SCULL_DEBUG
#ifdef SCULL_DEBUG
     /* This one if debugging is on, and kernel space */
	#define PDEBUG(fmt, args...) printk( KERN_WARNING "scull: " fmt, ## args)
#else
	#define PDEBUG(fmt, args...) /* not debugging: nothing */
#endif

MODULE_LICENSE("Dual BSD/GPL");
int size=100;
module_param(size, int, S_IRUGO);

int scull_major=0,scull_minor=0;

struct scull_pipe{
	wait_queue_head_t inq, outq;
	char *buffer, *end;
	int buffersize;
	char *rp, *wp;
	int nreaders, nwriters;
	struct fasync_struct *async_queue;
	struct semaphore sem;
	struct cdev cdev;
};

struct scull_pipe *dev;

int scull_p_open(struct inode* inode, struct file *fp){
	PDEBUG("open start!!");
	struct scull_pipe *dev;
	dev = container_of(inode->i_cdev, struct scull_pipe, cdev);
	fp->private_data = dev;
	if(down_interruptible(&dev->sem))
		return -ERESTARTSYS;
	if(fp->f_mode & FMODE_READ)
		dev->nreaders++;
	if(fp->f_mode & FMODE_WRITE)
		dev->nwriters++;
	up(&dev->sem);
	PDEBUG("open success!!");
	return nonseekable_open(inode, fp);
}

int scull_p_release(struct inode *inode, struct file *fp){
	struct scull_pipe *dev = fp->private_data;
	if(down_interruptible(&dev->sem))
		return ERESTARTSYS;
	if(fp->f_mode & FMODE_READ)
		dev->nreaders--;
	if(fp->f_mode & FMODE_WRITE)
		dev->nwriters--;
	up(&dev->sem);
	return 0;
}

static int spacefree(struct scull_pipe *dev){
	if(dev->rp == dev->wp)
		return dev->buffersize -1;
	return ((dev->rp + dev->buffersize - dev->wp) % dev->buffersize) - 1;
}

static int scull_getwritespace(struct scull_pipe *dev, struct file *filp){
	while(spacefree(dev) == 0){
		DEFINE_WAIT(wait);

		up(&dev->sem);
		if(filp->f_flags & O_NONBLOCK)
			return -EAGAIN;
		PDEBUG("\"%s\" writting: going to sleep\n", current->comm);
		prepare_to_wait(&dev->outq, &wait, TASK_INTERRUPTIBLE);
		if(spacefree(dev) ==0 )
			schedule();
		finish_wait(&dev->outq, &wait);
		if(signal_pending(current))
			return -ERESTARTSYS;
		if(down_interruptible(&dev->sem))
			return -ERESTARTSYS;
	}
	return 0;
}

static ssize_t scull_p_write(struct file *filp, const char __user *buff, size_t count, loff_t *f_pos){
	PDEBUG("writting starts");
	struct scull_pipe *dev = filp->private_data;
	int result;

	if(down_interruptible(&dev->sem))
		return -ERESTARTSYS;
	PDEBUG("scull_getwritespace() starts");
	result = scull_getwritespace(dev, filp);
	if(result)
		return result;
	count = min(count, (size_t)spacefree(dev));
	if(dev->wp >= dev->rp)
		count = min(count, (size_t)(dev->end - dev->wp));
	else
		count = min(count, (size_t)(dev->rp - dev->wp-1));
	PDEBUG("Going to accept %li bytes to %p from %p\n", (long)count, dev->wp, buff);
	if(copy_from_user(dev->wp, buff, count)){
		up(&dev->sem);
		return -EFAULT;
	}
	dev->wp += count;
	if(dev->wp == dev->end)
		dev->wp = dev->buffer;
	up(&dev->sem);

	wake_up_interruptible(&dev->inq);

	if(dev->async_queue)
		kill_fasync(&dev->async_queue, SIGIO, POLL_IN);

	PDEBUG("\"%s\" did write %li bytes\n", current->comm, count);
	return count;
}

static ssize_t scull_p_read(struct file *filp, char __user *buff, size_t count, loff_t *f_pos){
	struct scull_pipe *dev = filp->private_data;
	if(down_interruptible(&dev->sem))
		return -ERESTARTSYS;
	while(dev->rp == dev->wp){
		up(&dev->sem);
		if(filp->f_flags & O_NONBLOCK)
			return -EAGAIN;
		PDEBUG("\"%s\" reading: going to sleep\n", current->comm);
		if(wait_event_interruptible(dev->inq, (dev->rp != dev->wp)))
			return -ERESTARTSYS;
		if(down_interruptible(&dev->sem))
			return -ERESTARTSYS;
	}
	if(dev->wp > dev->rp)
		count = min(count, (size_t)(dev->wp - dev->rp));
	else
		count = min(count, (size_t)(dev->end - dev->rp));
	if(copy_to_user(buff, dev->rp, count)){
		up(&dev->sem);
		return -EFAULT;
	}
	dev->rp += count;
	if(dev->rp == dev->end)
		dev->rp = dev->buffer;
	up(&dev->sem);

	wake_up_interruptible(&dev->outq);
	PDEBUG("\"%s\" did read %li bytes \n", current->comm, (long)count);
	return count;
}

struct file_operations scull_pipe_fops={
	.open = scull_p_open,
	.write = scull_p_write,
	.read = scull_p_read,
	.release = scull_p_release,
};

static void scull_p_setup_cdev(struct scull_pipe *dev){
	int err;

	cdev_init(&dev->cdev, &scull_pipe_fops);
	dev->cdev.owner = THIS_MODULE;
	dev->cdev.ops = &scull_pipe_fops;
	err = cdev_add(&dev->cdev, MKDEV(scull_major,scull_minor), 1);
	if(err)
		printk(KERN_NOTICE "Error %d adding scull_pipe", err);
}

static int hello_init(void){
	int result;
	dev_t devno;
	if(scull_major){
		devno = MKDEV(scull_major, scull_minor);
		result = register_chrdev_region(devno, 1, DEV_NAME);
	}else{
		result=alloc_chrdev_region(&devno,scull_minor,1,DEV_NAME);
		scull_major = MAJOR(devno);
		scull_minor = MINOR(devno);
	}
	if(result<0){
		printk( KERN_WARNING "scull:can't get major %d\n",scull_major);
		return result;
	}
	dev = kmalloc(sizeof(struct scull_pipe), GFP_KERNEL);
	if(!dev){
		printk(KERN_NOTICE "Get memory fail!\n");
		return -ENOMEM;
	}
	memset(dev, 0, sizeof(struct scull_pipe));
	dev->buffersize = size;
	dev->buffer = kmalloc(size, GFP_KERNEL);
	if(!dev->buffer){
		printk(KERN_NOTICE "Get memory fail\n");
		return -ENOMEM;
	}
	dev->end = dev->buffer + size;
	dev->rp = dev->buffer;
	dev->wp = dev->buffer;
	dev->nreaders = dev->nwriters =0;
	sema_init(&dev->sem, 1);
	init_waitqueue_head(&dev->inq);
	init_waitqueue_head(&dev->outq);

	scull_p_setup_cdev(dev);
	PDEBUG("inmode success %d\n",size);
	#ifdef SCULL_DEBUG
	     /* This one if debugging is on, and kernel space */
		printk(KERN_WARNING"debug on\n");
	#else
		printk(KERN_WARNING"debug off\n");
	#endif
	
	return 0;
}

static void hello_exit(void){

	if(dev){
		cdev_del(&dev->cdev);
		kfree(dev->buffer);
		kfree(dev);
	}
	unregister_chrdev_region(MKDEV(scull_major,scull_minor),1);
	dev = NULL;
	PDEBUG("Goodbye\n");
}

module_init(hello_init);
module_exit(hello_exit);