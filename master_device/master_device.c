#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/net.h>
#include <net/sock.h>
#include <asm/processor.h>
#include <linux/netdevice.h>
#include <linux/ip.h>
#include <linux/in.h>
#include <linux/miscdevice.h>
#include <asm/uaccess.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/debugfs.h>
#include <linux/mm.h>
#include <linux/gfp.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include "../user_program/common.h"

#ifndef VM_RESERVED
#define VM_RESERVED   (VM_DONTEXPAND | VM_DONTDUMP)
#endif

#define DEV_NAME "master_device"
#define PRINTFUNC() printk(KERN_ALERT DEV_NAME ": %s called.\n", __func__)

#define DEFAULT_PORT 2325
#define master_IOCTL_CREATESOCK 0x12345677
#define master_IOCTL_MMAP 0x12345678
#define master_IOCTL_EXIT 0x12345679
#define BUF_SIZE 512

typedef struct socket * ksocket_t;

struct dentry  *file1;//debug file

//functions about kscoket are exported, and thus we use extern here
extern ksocket_t ksocket(int domain, int type, int protocol);
extern int kbind(ksocket_t socket, struct sockaddr *address, int address_len);
extern int klisten(ksocket_t socket, int backlog);
extern ksocket_t kaccept(ksocket_t socket, struct sockaddr *address, int *address_len);
extern ssize_t ksend(ksocket_t socket, const void *buffer, size_t length, int flags);
extern int kclose(ksocket_t socket);
extern char *inet_ntoa(struct in_addr *in);//DO NOT forget to kfree the return pointer

static int __init master_init(void);
static void __exit master_exit(void);

int master_close(struct inode *inode, struct file *filp);
int master_open(struct inode *inode, struct file *filp);
static long master_ioctl(struct file *file, unsigned int ioctl_num, unsigned long ioctl_param);
static ssize_t send_msg(struct file *file, const char __user *buf, size_t count, loff_t *data);//use when user is writing to this device

static ksocket_t sockfd_srv, sockfd_cli;//socket for master and socket for slave
static struct sockaddr_in addr_srv;//address for master
static struct sockaddr_in addr_cli;//address for slave
static mm_segment_t old_fs;
static int addr_len;

int mmap_fault(struct vm_fault *vmf)
{
	struct page* page;
	PRINTFUNC();
	page = virt_to_page(vmf->address);
	get_page(page);
	vmf->page = page;
    return 0;
}

void mmap_dummy_open(struct vm_area_struct *vma)  
{
	return;
}

void mmap_dummy_close(struct vm_area_struct *vma)  
{
	return;
}

// vm operations struct
static const struct vm_operations_struct custom_vm_ops = {
	.open = mmap_dummy_open,
	.close = mmap_dummy_close,
	.fault = mmap_fault
};

// Reference: https://www.xml.com/ldd/chapter/book/ch13.html
static int custom_mmap(struct file *filp, struct vm_area_struct *vma)
{
	PRINTFUNC();
	if (remap_pfn_range(
			vma,
			vma->vm_start,
			virt_to_phys(filp->private_data) >> PAGE_SHIFT,
			vma->vm_end - vma->vm_start,
			vma->vm_page_prot) < 0) {
		printk(KERN_ERR "custom_mmap remap_page_range failed!\n");
		return -1;
	}
	vma->vm_flags |= VM_RESERVED;
	vma->vm_ops = &custom_vm_ops;
	vma->vm_private_data = filp->private_data;
	
    mmap_dummy_open(vma);
    return 0;
}

//file operations
static struct file_operations master_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = master_ioctl,
	.open = master_open,
	.write = send_msg,
	.release = master_close,
	.mmap = custom_mmap
};

//device info
static struct miscdevice master_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "master_device",
	.fops = &master_fops
};

static int __init master_init(void)
{
	int ret;
	file1 = debugfs_create_file("master_debug", 0644, NULL, NULL, &master_fops);

	//register the device
	if( (ret = misc_register(&master_dev)) < 0){
		printk(KERN_ERR "misc_register failed!\n");
		return ret;
	}

	printk(KERN_INFO "master has been registered!\n");

	old_fs = get_fs();
	set_fs(KERNEL_DS);

	//initialize the master server
	sockfd_srv = sockfd_cli = NULL;
	memset(&addr_cli, 0, sizeof(addr_cli));
	memset(&addr_srv, 0, sizeof(addr_srv));
	addr_srv.sin_family = AF_INET;
	addr_srv.sin_port = htons(DEFAULT_PORT);
	// Note: INADDR_ANY means all IPs on this machine is being listened to.
	addr_srv.sin_addr.s_addr = INADDR_ANY;
	addr_len = sizeof(struct sockaddr_in);

	sockfd_srv = ksocket(AF_INET, SOCK_STREAM, 0);
	printk("sockfd_srv = 0x%p  socket is created \n", sockfd_srv);
	if (sockfd_srv == NULL)
	{
		printk("socket failed\n");
		return -1;
	}
	if (kbind(sockfd_srv, (struct sockaddr *)&addr_srv, addr_len) < 0)
	{
		printk("bind failed\n");
		return -1;
	}
	if (klisten(sockfd_srv, 10) < 0)
	{
		printk("listen failed\n");
		return -1;
	}
    printk("master_device init OK\n");
	set_fs(old_fs);
	return 0;
}

static void __exit master_exit(void)
{
	misc_deregister(&master_dev);
    printk("misc_deregister\n");
	if(kclose(sockfd_srv) == -1)
	{
		printk("kclose srv error\n");
		return ;
	}
	set_fs(old_fs);
	printk(KERN_INFO "master exited!\n");
	debugfs_remove(file1);
}

int master_close(struct inode *inode, struct file *filp)
{
	__free_pages(filp->private_data, SHIFT_ORDER);
	filp->private_data = NULL;
	return 0;
}

int master_open(struct inode *inode, struct file *filp)
{
	struct page *page_addr;
	page_addr = alloc_pages(GFP_KERNEL, SHIFT_ORDER);
	if(!page_addr) {
		return -ENOMEM;
	}
	// IMPORTANT!!
	// This address is "kernel virtual address",
	// use virt_to_phys() to convert later.
	filp->private_data = (unsigned long) page_address(page_addr);
	return 0;
}


static long master_ioctl(struct file *filp, unsigned int ioctl_num, unsigned long ioctl_param)
{
	long ret = -EINVAL;
	size_t len = 0, data_size = 0, file_size = 0;
	unsigned long offset = 0;
	char *tmp;
	void *buf_addr = NULL;
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;
    pte_t *ptep, pte;
	old_fs = get_fs();
	set_fs(KERNEL_DS);
	switch(ioctl_num){
		case master_IOCTL_CREATESOCK:// create socket and accept a connection
			sockfd_cli = kaccept(sockfd_srv, (struct sockaddr *)&addr_cli, &addr_len);
			if (sockfd_cli == NULL)
			{
				printk("accept failed\n");
				return -1;
			}
			else
				printk("aceept sockfd_cli = 0x%p\n", sockfd_cli);

			tmp = inet_ntoa(&addr_cli.sin_addr);
			printk("got connected from : %s %d\n", tmp, ntohs(addr_cli.sin_port));
			kfree(tmp);
			ret = 0;
			break;
		case master_IOCTL_MMAP:
			// ioctl_param is the len to be sent (in bytes).
			len = (size_t) ioctl_param;
			buf_addr = (void *) filp->private_data;
      		len = ksend(sockfd_cli, buf_addr, len, 0);
    		ret = len;
			break;
		case master_IOCTL_EXIT:
			if(kclose(sockfd_cli) == -1)
			{
				printk("kclose cli error\n");
				return -1;
			}
			ret = 0;
			break;
		default:
			pgd = pgd_offset(current->mm, ioctl_param);
			p4d = p4d_offset(pgd, ioctl_param);
			pud = pud_offset(p4d, ioctl_param);
			pmd = pmd_offset(pud, ioctl_param);
			ptep = pte_offset_kernel(pmd , ioctl_param);
			pte = *ptep;
			printk("master: %lX\n", pte);
			ret = 0;
			break;
	}

	set_fs(old_fs);
	return ret;
}
static ssize_t send_msg(struct file *file, const char __user *buf, size_t count, loff_t *data)
{
//call when user is writing to this device
	char msg[BUF_SIZE];
	if(copy_from_user(msg, buf, count))
		return -ENOMEM;
	ksend(sockfd_cli, msg, count, 0);

	return count;

}

module_init(master_init);
module_exit(master_exit);
MODULE_LICENSE("GPL");
