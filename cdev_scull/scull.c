#include "scull.h"

int scull_trim(struct scull_dev *dev){
	struct scull_qset *next, *dptr;
	int qset=dev->qset;
	int i=0;
	for(dptr=dev->data;dptr;dptr=next){
		if(dptr->data){
			for(i=0;i<qset;i++)
				kfree(dptr->data[i]);
			kfree(dptr->data);
			dptr->data=NULL;
		}
		next=dptr->next;
		kfree(dptr);
	}
	dev->size=0;
	dev->quantum = 4000;
	dev->qset = 1000;
	dev->data=NULL;
	return 0;
}

struct scull_qset* scull_follow(struct scull_dev *dev, int item){
	struct scull_qset *dptr;
	if(!dev->data){
		dev->data = kmalloc(sizeof(struct scull_qset),GFP_KERNEL);
		if(!dev->data)
			return NULL;
		memset(dev->data, 0, sizeof(struct scull_qset));
	}
	dptr=dev->data;
	int i=0;
	for(i=0; i < item; i++){
		if(!dptr->next){
			dptr->next = kmalloc(sizeof(struct scull_qset),GFP_KERNEL);
			if(!dptr->next)
				return NULL;
			memset(dptr->next, 0, sizeof(struct scull_qset));
		}
		dptr=dptr->next;
	}
	return dptr;
}

long int scull_ioctl (struct file *filp,unsigned int cmd, unsigned long arg){
	int err = 0, tmp=0;
	int retval = 0;

	if(_IOC_TYPE_GARY(cmd) != SCULL_IOC_MAGIC) return -ENOTTY;
	if(_IOC_NR(cmd) > SCULL_IOC_MAXNR) return -ENOTTY;

	//_IOC_READ means that user want to get data from device. So the device will write data to the ptr. We need to varify the write access
	if(_IOC_DIR(cmd) & _IOC_READ)
		err = !access_ok((void __user *)arg, _IOC_SIZE(cmd));
	else if(_IOC_DIR(cmd) & _IOC_WRITE){
		err = !access_ok((void __user *)arg, _IOC_SIZE(cmd));
	}
	if(err)
		return -EFAULT;

	switch(cmd){
		case SCULL_IOCSQUANTUM:
			if(!capable(CAP_SYS_ADMIN))
				return -EPERM;
			retval = __get_user(tmp, (int __user *)arg);
			printk("SCULL_IOCSQUANTUM:%d\n",tmp);
			break;
		case SCULL_IOCGQUANTUM:
			retval=__put_user(scull_quantum, (int __user *)arg);
			break;
		default:
			return -ENOTTY;
	}
	return retval;
}

ssize_t scull_read(struct file *filp, char __user *buff, size_t count, loff_t *f_pos){
	printk(KERN_NOTICE"scull_read !!");
	struct scull_qset *dptr;
	int quantum = dev->quantum, qset = dev->qset;
	int itemsize = quantum * qset;
	int item, s_pos, q_pos, rest;
	ssize_t retval = 0;

	if(down_interruptible(&dev->sem)){
		return -ERESTARTSYS;
	} 

	if(*f_pos >= dev->size)
		goto out;
	if(*f_pos + count > dev->size)
		count=dev->size - *f_pos;

	item = (long)*f_pos/itemsize;
	rest = (long)*f_pos%itemsize;
	s_pos = rest / quantum;
	q_pos = rest % quantum;

	dptr = scull_follow(dev, item);

	if(dptr == NULL || !dptr->data || !dptr->data[s_pos])
		goto out;

	if(count > quantum - q_pos)
		count = quantum - q_pos;

	if(copy_to_user(buff, dptr->data[s_pos] + q_pos, count)){
		retval = -EFAULT;
		goto out;
	}
	*f_pos += count;
	retval = count;

  out:
  	up(&dev->sem);
  	return retval;
}

ssize_t scull_write(struct file *filp, const char __user *buff, size_t count, loff_t *f_pos){
	printk(KERN_NOTICE"scull_write !!");
	struct scull_dev *dev = filp->private_data;
	struct scull_qset *dptr;
	int quantum = dev->quantum, qset = dev->qset;
	int itemsize = quantum * qset;
	int item, s_pos, q_pos,rest;
	ssize_t retval = -ENOMEM;

	if(down_interruptible(&dev->sem)){
		return -ERESTARTSYS;
	}

	item = (long)*f_pos/itemsize;
	rest = (long)*f_pos%itemsize;
	s_pos = rest / quantum;
	q_pos = rest % quantum;

	dptr = scull_follow(dev, item);
	if(dptr==NULL)
		goto out;
	if(!dptr->data){
		dptr->data = kmalloc(qset *sizeof(char *),GFP_KERNEL);
		if(!dptr->data)
			goto out ;
		memset(dptr->data, 0, qset*sizeof(char *));
	}
	if(!dptr->data[s_pos]){
		dptr->data[s_pos] = kmalloc(quantum, GFP_KERNEL);
		if(!dptr->data[s_pos])
			goto out;
	}
	if(count > quantum - q_pos)
		count = quantum - q_pos;
	if(copy_from_user(dptr->data[s_pos] + q_pos, buff, count)){
		retval = -EFAULT;
		goto out;
	}
	*f_pos += count;
	retval = count;

	if(dev->size < *f_pos)
		dev->size = *f_pos;
	out :
	up(&dev->sem);
	return retval;
}

int scull_open(struct inode *inode,struct file *filp){
	struct scull_dev *dev;

	dev = container_of(inode->i_cdev,struct scull_dev,cdev);
	filp->private_data=dev;

	if ((filp->f_flags & O_ACCMODE)==O_WRONLY)
	{
		scull_trim(dev);
	}
	return 0;
}


static void scull_setup_cdev(struct scull_dev *dev, int index){
	int err;

	cdev_init(&dev->cdev, &scull_fops);
	dev->cdev.owner = THIS_MODULE;
	dev->cdev.ops = &scull_fops;
	err = cdev_add(&dev->cdev, devno, 1);
	if(err)
		printk(KERN_NOTICE "Error %d adding scull%d", err, index);
}

static int __init scull_init(void){
	int result;

	ent=proc_create("scull_proc",0660,NULL,&scull_proc_fops);
	if(!ent){
		return -1;
	}
	if(scull_major){
		devno=MKDEV(scull_major,scull_minor);
		result=register_chrdev_region(devno,scull_nr_devs,DEV_NAME);
	}else{
		result=alloc_chrdev_region(&devno,scull_minor,scull_nr_devs,DEV_NAME);
	}
	if(result<0){
		printk( KERN_WARNING "scull:can't get major %d\n",scull_major);
		return result;
	}

	scull_major = MAJOR(devno);
	scull_minor = MINOR(devno);
	dev = kmalloc(sizeof(struct scull_dev), GFP_KERNEL);
	if(!dev){
		printk(KERN_NOTICE "Get memory failed!");
		return -ENOMEM;
	}
	memset(dev, 0, sizeof(struct scull_dev));

	dev->quantum = 4000;
	dev->qset = 1000;

	sema_init(&dev->sem, 1);

	scull_setup_cdev(dev, 0);
	printk(KERN_NOTICE "insmode success!%d %d\n",2,scull_major);
	return 0;
}

static void __exit scull_exit(void){
	if(dev){
		scull_trim(dev);
		cdev_del(&dev->cdev);
		kfree(dev);
	}
	proc_remove(ent);
	unregister_chrdev_region(devno,1);
	printk(KERN_NOTICE"rmmod success!");
}


module_init(scull_init);
module_exit(scull_exit);

MODULE_LICENSE("GPL");
