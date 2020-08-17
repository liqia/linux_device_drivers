#include <linux/init.h>
#include <linux/module.h>

MODULE_LICENSE("Dual BSD/GPL");
int size;
module_param(size, int, S_IRUGO);

struct scull_pipe{
	wait_queue_head_t inq, outq;
	char *buffer, *end;
	int buffersize;
	char *rp, *wp;
	int nreaders, nwriters;
	struct fasync_struct *async_queue;
	struct semaphore sem;
	struct cdev cdev;
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
	struct scull_pipe *dev = filp->private_data;
	int result;

	if(down_interruptible(&dev->sem))
		return -ERESTARTSYS;
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
		up(dev->sem);
		return -EFAULT;
	}
	dev->rp += count;
	if(dev->rp == dev->end)
		dev->rp = dev->buff;
	up(&dev->sem);

	wake_up_interruptible(&dev->outq);
	PDEBUG("\"%s\" did read %li bytes \n", current->comm, (long)count);
	return count;
}



static int hello_init(void){
	printk(KERN_ALERT"Hello world\n");
	return 0;
}

static void hello_exit(void){
	printk(KERN_ALERT"Goodbye\n");

}

module_init(hello_init);
module_exit(hello_exit);