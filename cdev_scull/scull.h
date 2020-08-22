#include <linux/cdev.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include "scull_ioctl.h"

#define SCULL_MAJOR 0
#define DEV_NAME "scull"
int scull_major=0,scull_minor=0;
int scull_nr_devs=1;
int scull_quantum=999;
dev_t devno=0;
static struct proc_dir_entry *ent;

struct scull_qset{
	void **data;
	struct scull_qset *next;
};

struct scull_dev{
	struct scull_qset *data;
	int quantum;
	int qset;
	unsigned long size;
	unsigned int access_key;
	struct semaphore sem;
	struct cdev cdev;
} ;

struct scull_dev *dev;

//this function is used to release all data in data field
int scull_trim(struct scull_dev *dev);

//find item-th scull_qset
struct scull_qset* scull_follow(struct scull_dev *dev, int item);

int scull_open(struct inode *inode,struct file *filp);

int scull_release(struct inode *inode,struct file *filp){
	return 0;
}

ssize_t scull_read(struct file *filp,  char __user *  buff, size_t count, loff_t *f_pos);

ssize_t scull_write(struct file *filp, const char __user *buff, size_t count, loff_t *f_pos);

long int scull_ioctl (struct file *filp,unsigned int cmd, unsigned long arg);

loff_t scull_llseek(struct file *fp, loff_t off, int whence);

struct file_operations scull_fops={
	.owner =    THIS_MODULE,
	.read =     scull_read,
	.write =    scull_write,
	.open =     scull_open,
	.llseek =   scull_llseek,
	.release =  scull_release,
	.unlocked_ioctl =   	scull_ioctl,
};


struct file_operations scull_proc_fops={
	.owner =    THIS_MODULE,
	.read =     scull_read,
};

ssize_t scull_proc_read(struct file *file, char __user *ubuff, size_t count, loff_t *ppos);

ssize_t scull_proc_write(struct file *file, const char __user *ubuff, size_t count, loff_t *ppos);