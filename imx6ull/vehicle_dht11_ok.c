/*
 * 文件说明：
 * 这是一个基于设备树匹配的DHT11字符设备驱动。
 * 本文件只增加中文注释，不改变原有代码逻辑、接口、时序和数据解析方式。
 */

/* 引入通用基础错误码定义，例如-EINVAL、EIO等。 */
#include "asm-generic/errno-base.h"

/* 引入旧式整数GPIO接口相关声明，例如gpio_request、gpio_free等。 */
#include "asm-generic/gpio.h"

/* 引入jiffies相关定义，用于设置内核定时器到期时间。 */
#include "linux/jiffies.h"

/* 引入Linux内核模块基础接口，例如MODULE_LICENSE、THIS_MODULE等。 */
#include <linux/module.h>

/* 引入poll机制相关定义；当前代码中实际上没有使用poll接口。 */
#include <linux/poll.h>

/* 引入延时函数，例如mdelay()。 */
#include <linux/delay.h>

/* 引入文件系统和file_operations相关定义。 */
#include <linux/fs.h>

/* 引入Linux错误码相关定义。 */
#include <linux/errno.h>

/* 引入misc设备相关定义；当前代码中并未使用miscdevice。 */
#include <linux/miscdevice.h>

/* 引入内核常用宏、类型和printk等接口。 */
#include <linux/kernel.h>

/* 引入传统主设备号相关定义。 */
#include <linux/major.h>

/* 引入互斥锁相关定义；当前代码中没有实际使用mutex。 */
#include <linux/mutex.h>

/* 引入proc文件系统相关定义；当前代码中没有使用proc接口。 */
#include <linux/proc_fs.h>

/* 引入seq_file相关定义；当前代码中没有使用seq_file。 */
#include <linux/seq_file.h>

/* 引入stat权限相关定义。 */
#include <linux/stat.h>

/* 引入内核初始化相关宏。 */
#include <linux/init.h>

/* 引入class_create、device_create等设备模型接口。 */
#include <linux/device.h>

/* 引入TTY相关接口；当前代码中没有使用。 */
#include <linux/tty.h>

/* 引入内核模块自动加载相关接口；当前代码中没有使用。 */
#include <linux/kmod.h>

/* 引入内核内存分配标志，例如GFP_KERNEL。 */
#include <linux/gfp.h>

/* 引入新式GPIO描述符接口；当前代码主要使用旧式整数GPIO接口。 */
#include <linux/gpio/consumer.h>

/* 引入platform_device和platform_driver相关定义。 */
#include <linux/platform_device.h>

/* 引入设备树基础类型和匹配接口。 */
#include <linux/of.h>

/* 引入从设备树读取GPIO属性的接口，例如of_get_named_gpio()。 */
#include <linux/of_gpio.h>

/* 引入设备树中断相关接口；当前代码中没有直接使用。 */
#include <linux/of_irq.h>

/* 引入中断申请、释放和中断返回值相关定义。 */
#include <linux/interrupt.h>

/* 引入IRQ相关定义。 */
#include <linux/irq.h>

/* 引入内核内存分配相关定义；当前代码中没有直接使用kmalloc。 */
#include <linux/slab.h>

/* 引入文件控制标志相关定义。 */
#include <linux/fcntl.h>

/* 引入Linux内核定时器相关接口。 */
#include <linux/timer.h>

/*
 * 定义一个结构体，用来描述DHT11所使用的GPIO和相关资源。
 * 注意：这个名字与内核中的struct gpio_desc同名，容易产生混淆，
 * 但为了保持原代码不变，这里不做重命名。
 */
struct gpio_desc{

	/* 保存Linux全局GPIO编号，例如当前设备树解析后通常得到115。 */
	int gpio;

	/* 保存由GPIO转换得到的中断号。 */
	int irq;

	/* 保存GPIO的名称，用于gpio_request和request_irq时标识资源。 */
    char *name;

	/* 当前代码中没有实际使用这个成员，保留原结构。 */
    int key;

	/* 保存DHT11采集超时所使用的内核定时器。 */
	struct timer_list key_timer;

/* 结束结构体定义。 */
} ;

/*
 * 定义一个GPIO描述数组。
 * 当前只有一个DHT11数据引脚，所以数组中只有一个元素。
 */
static struct gpio_desc gpios[] = {

	/*
	 * gpio初始值为-1，表示尚未从设备树中获取；
	 * irq初始值为0；
	 * name设置为"dht11"；
	 * 其余成员自动初始化为0。
	 */
    {-1, 0, "dht11", },

/* 结束GPIO数组定义。 */
};

/* 保存register_chrdev()动态分配得到的主设备号，0表示尚未分配。 */
static int major = 0;

/* 保存class_create()创建的设备类指针。 */
static struct class *gpio_class;

/*
 * 保存每次GPIO边沿中断发生时的纳秒时间戳。
 * 数组长度84来自原驱动的经验值：
 * 一次完整通信通常捕获81到84个边沿。
 */
static u64 g_dht11_irq_time[84];

/* 记录当前一次采集中已经捕获到多少个边沿中断。 */
static int g_dht11_irq_cnt = 0;

/* 定义环形缓冲区总容量为128字节。 */
#define BUF_LEN 128

/* 定义用于保存解析结果的环形缓冲区。 */
static char g_keys[BUF_LEN];

/* r是环形缓冲区读位置，w是写位置。 */
static int r, w;

/*
 * 定义异步通知结构指针。
 * 当前驱动并没有实现fasync，所以该变量实际上没有被使用。
 */
struct fasync_struct *button_fasync;

/* 提前声明DHT11中断处理函数。 */
static irqreturn_t dht11_isr(int irq, void *dev_id);

/* 提前声明DHT11数据解析函数。 */
static void parse_dht11_datas(void);

/*
 * 计算环形缓冲区的下一个位置。
 * 当位置加1达到BUF_LEN时，通过取模回到0。
 */
#define NEXT_POS(x) ((x+1) % BUF_LEN)

/* 定义函数：判断环形缓冲区是否为空。 */
static int is_key_buf_empty(void)

/* 函数体开始。 */
{

	/*
	 * 当读位置r和写位置w相等时，
	 * 表示当前没有未读取的数据。
	 */
	return (r == w);

/* 函数体结束。 */
}

/* 定义函数：判断环形缓冲区是否已满。 */
static int is_key_buf_full(void)

/* 函数体开始。 */
{

	/*
	 * 如果读位置r正好等于写位置w的下一个位置，
	 * 说明环形缓冲区已经没有可写空间。
	 */
	return (r == NEXT_POS(w));

/* 函数体结束。 */
}

/* 定义函数：向环形缓冲区写入一个字节。 */
static void put_key(char key)

/* 函数体开始。 */
{

	/* 只有缓冲区未满时才允许写入。 */
	if (!is_key_buf_full())

	/* if代码块开始。 */
	{

		/* 将传入的数据写入当前写位置w。 */
		g_keys[w] = key;

		/* 写位置移动到下一个环形位置。 */
		w = NEXT_POS(w);

	/* if代码块结束。 */
	}

/* 函数体结束。 */
}

/* 定义函数：从环形缓冲区取出一个字节。 */
static char get_key(void)

/* 函数体开始。 */
{

	/*
	 * 默认返回0。
	 * 如果缓冲区为空，就直接返回这个默认值。
	 */
	char key = 0;

	/* 只有缓冲区非空时才读取数据。 */
	if (!is_key_buf_empty())

	/* if代码块开始。 */
	{

		/* 从当前读位置r取出一个字节。 */
		key = g_keys[r];

		/* 读位置移动到下一个环形位置。 */
		r = NEXT_POS(r);

	/* if代码块结束。 */
	}

	/* 返回取得的数据。 */
	return key;

/* 函数体结束。 */
}

/*
 * 定义并初始化一个等待队列。
 * read()会睡眠在这个等待队列上，
 * 数据解析完成后会唤醒它。
 */
static DECLARE_WAIT_QUEUE_HEAD(gpio_wait);

/*
 * 旧内核中timer回调函数使用unsigned long参数。
 * 注释中的新式timer回调写法当前未启用。
 */
// static void key_timer_expire(struct timer_list *t)

/* 定义旧式内核定时器回调函数。 */
static void key_timer_expire(unsigned long data)

/* 函数体开始。 */
{

	/*
	 * 定时器到期时，直接调用数据解析函数。
	 * 如果采集到的边沿数量不足，解析函数会返回错误结果。
	 */
	parse_dht11_datas();

/* 函数体结束。 */
}

/*
 * 定义字符设备的read()方法。
 * 用户空间每调用一次read()，驱动就主动发起一次DHT11采集。
 */
static ssize_t dht11_read (struct file *file, char __user *buf, size_t size, loff_t *offset)

/* 函数体开始。 */
{

	/*
	 * err用于保存GPIO、中断申请、用户空间复制等接口的返回值。
	 */
	int err;

	/*
	 * 驱动最终向用户空间返回两个字节：
	 * kern_buf[0]保存湿度整数；
	 * kern_buf[1]保存温度整数。
	 */
	char kern_buf[2];

	/*
	 * 旧驱动规定用户空间一次必须正好读取2字节。
	 * 如果请求大小不是2，直接返回参数错误。
	 */
	if (size != 2)

		/* 返回-EINVAL，表示参数无效。 */
		return -EINVAL;

	/*
	 * 开始新一轮采集前，
	 * 将边沿中断计数清零。
	 */
	g_dht11_irq_cnt = 0;

	/*
	 * 第一步：主机向DHT11发送启动信号。
	 * 主机需要将数据线拉低至少18ms。
	 */

	/* 申请该GPIO的临时使用权。 */
	err = gpio_request(gpios[0].gpio, gpios[0].name);

	/* 将GPIO设置为输出模式，并输出低电平。 */
	gpio_direction_output(gpios[0].gpio, 0);

	/*
	 * 设置完成后立即释放GPIO资源。
	 * 原驱动依赖这种使用方式，因此这里保持不变。
	 */
	gpio_free(gpios[0].gpio);

	/* 忙等待18ms，保证DHT11识别到启动信号。 */
	mdelay(18);

	/*
	 * 将GPIO切换为输入模式。
	 * 此时主机释放数据线，数据线由上拉电阻拉高，
	 * DHT11随后可以主动拉低并返回响应。
	 */
	gpio_direction_input(gpios[0].gpio);

	/*
	 * 第二步：注册双边沿中断。
	 * 上升沿和下降沿都会进入dht11_isr()，
	 * 驱动通过记录边沿时间来判断每一位是0还是1。
	 */
	err = request_irq(gpios[0].irq, dht11_isr, IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING, gpios[0].name, &gpios[0]);

	/*
	 * 启动超时定时器。
	 * jiffies + 20表示20个系统节拍之后超时，
	 * 并不一定等于20ms，具体取决于内核HZ配置。
	 */
	mod_timer(&gpios[0].key_timer, jiffies + 20);	

	/*
	 * 第三步：当前进程睡眠，
	 * 直到环形缓冲区中出现数据。
	 * 数据可能来自正常解析，也可能来自超时错误结果。
	 */
	wait_event_interruptible(gpio_wait, !is_key_buf_empty());

	/*
	 * 本次采集完成后释放中断。
	 * 下次read()时会重新申请中断。
	 */
	free_irq(gpios[0].irq, &gpios[0]);

	/*
	 * 这条调试打印被注释掉，
	 * 不会参与编译。
	 */
	//printk("%s %s %d\n", __FILE__, __FUNCTION__, __LINE__);

	/*
	 * 采集结束后，将DHT11数据线恢复为空闲高电平。
	 */

	/* 再次申请GPIO。 */
	err = gpio_request(gpios[0].gpio, gpios[0].name);

	/* 如果申请失败，就打印源码文件、函数名和行号。 */
	if (err)

	/* if代码块开始。 */
	{

		/* 输出GPIO申请失败日志。 */
		printk("%s %s %d, gpio_request err\n", __FILE__, __FUNCTION__, __LINE__);

	/* if代码块结束。 */
	}

	/* 将GPIO设置为输出高电平。 */
	gpio_direction_output(gpios[0].gpio, 1);

	/* 释放GPIO资源。 */
	gpio_free(gpios[0].gpio);

	/*
	 * 第四步：从环形缓冲区取出两个结果字节。
	 */

	/* 第一个字节是湿度整数。 */
	kern_buf[0] = get_key();

	/* 第二个字节是温度整数。 */
	kern_buf[1] = get_key();

	/*
	 * 打印读取到的两个字节。
	 * %x表示按十六进制打印。
	 */
	printk("get val : 0x%x, 0x%x\n", kern_buf[0], kern_buf[1]);

	/*
	 * 如果两个字节都等于-1，
	 * 表示边沿数量不足或CRC校验失败。
	 */
	if ((kern_buf[0] == (char)-1) && (kern_buf[1] == (char)-1))

	/* if代码块开始。 */
	{

		/* 打印读取失败日志。 */
		printk("get err val\n");

		/* 返回-EIO，用户空间看到Input/output error。 */
		return -EIO;

	/* if代码块结束。 */
	}

	/*
	 * 将内核缓冲区中的两个字节复制到用户空间buf。
	 * copy_to_user返回未成功复制的字节数量。
	 */
	err = copy_to_user(buf, kern_buf, 2);

	/*
	 * 原代码无论copy_to_user是否成功，
	 * 都返回2，表示读取了2字节。
	 */
	return 2;

/* read函数结束。 */
}

/*
 * 定义字符设备release()方法。
 * 当用户空间关闭设备文件时调用。
 */
static int dht11_release (struct inode *inode, struct file *filp)

/* 函数体开始。 */
{

	/* 当前没有需要释放的私有资源，直接返回成功。 */
	return 0;

/* 函数体结束。 */
}

/*
 * 定义字符设备操作集合。
 * 内核通过该结构找到read和release函数。
 */
static struct file_operations dht11_drv = {

	/*
	 * 指定该字符设备属于当前模块，
	 * 防止设备正在使用时模块被卸载。
	 */
	.owner	 = THIS_MODULE,

	/* 将字符设备read操作绑定到dht11_read函数。 */
	.read    = dht11_read,

	/* 将字符设备release操作绑定到dht11_release函数。 */
	.release = dht11_release,

/* file_operations结构体初始化结束。 */
};

/*
 * 定义DHT11数据解析函数。
 * 它根据边沿时间间隔解析40位数据。
 */
static void parse_dht11_datas(void)

/* 函数体开始。 */
{

	/* 循环变量，用于遍历边沿时间戳。 */
	int i;

	/* 保存某一位数据高电平持续时间，单位为纳秒。 */
	u64 high_time;

	/* 临时保存正在拼接的一个字节。 */
	unsigned char data = 0;

	/* 记录当前已经拼接了多少位。 */
	int bits = 0;

	/* 保存DHT11返回的5个字节。 */
	unsigned char datas[5];

	/* 记录当前正在写datas数组的第几个字节。 */
	int byte = 0;

	/* 保存前4个字节计算得到的校验和。 */
	unsigned char crc;

	/* 打印本次采集捕获到的边沿数量。 */
	printk("g_dht11_irq_cnt = %d\n", g_dht11_irq_cnt);

	/*
	 * 一次完整通信通常捕获81到84个边沿。
	 * 少于81个说明数据不完整。
	 */
	if (g_dht11_irq_cnt < 81)

	/* if代码块开始。 */
	{

		/* 向环形缓冲区写入第一个错误标记-1。 */
		put_key(-1);

		/* 向环形缓冲区写入第二个错误标记-1。 */
		put_key(-1);

		/*
		 * 唤醒正在read()中等待的用户进程。
		 * 进程随后会读到两个-1并返回-EIO。
		 */
		wake_up_interruptible(&gpio_wait);

		/* 清零边沿计数，为下一次采集做准备。 */
		g_dht11_irq_cnt = 0;

		/* 立即结束解析函数。 */
		return;

	/* if代码块结束。 */
	}

	/*
	 * 从最后80个边沿中解析40位数据。
	 * 每一位数据包含一个上升沿和一个下降沿，
	 * 所以40位总共对应80个边沿。
	 */
	for (i = g_dht11_irq_cnt - 80; i < g_dht11_irq_cnt; i+=2)

	/* for循环体开始。 */
	{

		/*
		 * 当前边沿时间减去前一个边沿时间，
		 * 得到某一位数据高电平持续时间。
		 */
		high_time = g_dht11_irq_time[i] - g_dht11_irq_time[i-1];

		/*
		 * 将当前临时字节整体左移1位，
		 * 为新解析出的最低位腾出位置。
		 */
		data <<= 1;

		/*
		 * DHT11的数据1高电平约70us，
		 * 数据0高电平约26到28us。
		 * 这里用50us，也就是50000ns作为判断阈值。
		 */
		if (high_time > 50000)

		/* if代码块开始。 */
		{

			/* 将当前最低位置1，表示解析到数据1。 */
			data |= 1;

		/* if代码块结束。 */
		}

		/* 已解析位数加1。 */
		bits++;

		/* 每累计8位，就得到一个完整字节。 */
		if (bits == 8)

		/* if代码块开始。 */
		{

			/* 将拼接完成的字节保存到datas数组。 */
			datas[byte] = data;

			/* 清空临时字节，准备拼接下一个字节。 */
			data = 0;

			/* 位计数清零。 */
			bits = 0;

			/* 字节下标加1。 */
			byte++;

		/* if代码块结束。 */
		}

	/* for循环体结束。 */
	}

	/*
	 * DHT11第5个字节是校验和。
	 * 这里将前4个字节相加，结果自动截断为8位。
	 */
	crc = datas[0] + datas[1] + datas[2] + datas[3];

	/* 判断计算得到的校验和是否等于第5个字节。 */
	if (crc == datas[4])

	/* CRC正确分支开始。 */
	{

		/*
		 * datas[0]是湿度整数部分，
		 * 将其放入环形缓冲区。
		 */
		put_key(datas[0]);

		/*
		 * datas[2]是温度整数部分，
		 * 将其放入环形缓冲区。
		 */
		put_key(datas[2]);

	/* CRC正确分支结束。 */
	}

	/* CRC不正确时进入else分支。 */
	else

	/* else代码块开始。 */
	{

		/* 打印CRC校验失败日志。 */
		printk("dht11 crc err\n");

		/* 放入第一个错误标记。 */
		put_key(-1);

		/* 放入第二个错误标记。 */
		put_key(-1);

	/* else代码块结束。 */
	}

	/* 本次解析结束后清零边沿计数。 */
	g_dht11_irq_cnt = 0;

	/* 唤醒正在等待结果的read()进程。 */
	wake_up_interruptible(&gpio_wait);

/* 解析函数结束。 */
}

/*
 * 定义GPIO中断处理函数。
 * 每次数据线上出现上升沿或下降沿都会进入这里。
 */
static irqreturn_t dht11_isr(int irq, void *dev_id)

/* 函数体开始。 */
{

	/*
	 * 将request_irq时传入的dev_id转换为自定义GPIO结构体指针。
	 */
	struct gpio_desc *gpio_desc = dev_id;

	/* 保存当前中断发生时的纳秒时间戳。 */
	u64 time;

	/* 使用单调时钟取得当前时间，单位为纳秒。 */
	time = ktime_get_ns();

	/*
	 * 将当前边沿时间保存到时间戳数组。
	 * 数组下标就是当前边沿计数值。
	 */
	g_dht11_irq_time[g_dht11_irq_cnt] = time;

	/* 边沿计数加1。 */
	g_dht11_irq_cnt++;

	/*
	 * 原驱动认为最多记录84个边沿。
	 * 达到84个时，不再等待定时器，而是立即解析。
	 */
	if (g_dht11_irq_cnt == 84)

	/* if代码块开始。 */
	{

		/*
		 * 删除尚未到期的定时器，
		 * 避免之后再次调用解析函数。
		 */
		del_timer(&gpio_desc->key_timer);

		/* 立即解析当前已经采集完整的数据。 */
		parse_dht11_datas();

	/* if代码块结束。 */
	}

	/* 告诉内核该中断已经被本驱动处理。 */
	return IRQ_HANDLED;

/* 中断处理函数结束。 */
}

/*
 * 定义platform_driver的probe函数。
 * 当设备树节点与驱动compatible匹配成功后，内核会调用此函数。
 */
static int dht11_probe(struct platform_device *pdev)

/* 函数体开始。 */
{

	/* 保存各类内核接口返回值。 */
    int err;

	/* 循环变量。 */
    int i;

	/* 计算gpios数组中共有多少个元素。 */
    int count = sizeof(gpios)/sizeof(gpios[0]);

	/*
	 * 从当前设备树节点的data-gpios属性中读取第0个GPIO。
	 * 当前设备树中该属性指向GPIO4_IO19，
	 * 在旧式整数GPIO体系中通常会解析成115。
	 */
	gpios[0].gpio = of_get_named_gpio(pdev->dev.of_node, "data-gpios", 0);

	/*
	 * 如果GPIO控制器驱动还没有准备好，
	 * of_get_named_gpio会返回-EPROBE_DEFER，
	 * 告诉内核稍后重新尝试probe。
	 */
	if (gpios[0].gpio == -EPROBE_DEFER)

		/* 将-EPROBE_DEFER原样返回给platform总线。 */
		return -EPROBE_DEFER;

	/* 检查设备树解析得到的GPIO编号是否有效。 */
	if (!gpio_is_valid(gpios[0].gpio))

		/* GPIO无效时返回-EINVAL。 */
		return -EINVAL;

	/*
	 * 打印当前源码文件、函数名和行号，
	 * 用于确认probe确实被执行。
	 */
	printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);

	/* 遍历所有GPIO描述项。当前实际上只有一个。 */
	for (i = 0; i < count; i++)

	/* for循环体开始。 */
	{		

		/*
		 * 将Linux全局GPIO编号转换成对应的IRQ中断号。
		 */
		gpios[i].irq  = gpio_to_irq(gpios[i].gpio);

		/*
		 * 初始化时将DHT11数据线设置为输出高电平，
		 * 让总线保持空闲状态。
		 */

		/* 申请GPIO使用权。 */
		err = gpio_request(gpios[i].gpio, gpios[i].name);

		/* 将GPIO设置为输出高电平。 */
		gpio_direction_output(gpios[i].gpio, 1);

		/* 设置完成后释放GPIO。 */
		gpio_free(gpios[i].gpio);

		/*
		 * 初始化内核定时器：
		 * 到期时调用key_timer_expire()，
		 * data参数保存当前GPIO结构体地址。
		 */
		setup_timer(&gpios[i].key_timer, key_timer_expire, (unsigned long)&gpios[i]);

		/*
		 * 下面是适用于新内核timer_setup()的写法，
		 * 当前Linux 4.9版本未使用。
		 */
	 	//timer_setup(&gpios[i].key_timer, key_timer_expire, 0);

		/*
		 * 下面这行是曾经用于手动设置timer到期时间的实验代码，
		 * 当前被注释，不参与运行。
		 */
		//gpios[i].key_timer.expires = ~0;

		/*
		 * 下面这行是曾经用于立即加入定时器的实验代码，
		 * 当前被注释。
		 */
		//add_timer(&gpios[i].key_timer);

		/*
		 * 下面这行是曾经考虑在probe时长期注册IRQ的实验代码，
		 * 当前被注释。
		 */
		//err = request_irq(gpios[i].irq, dht11_isr, IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING, "100ask_gpio_key", &gpios[i]);

	/* for循环体结束。 */
	}

	/*
	 * 注册字符设备。
	 * 参数0表示让内核动态分配主设备号。
	 * 字符设备名称为vehicle_dht11。
	 */
	major = register_chrdev(0, "vehicle_dht11", &dht11_drv);

	/*
	 * 创建sysfs设备类。
	 * 成功后会出现/sys/class/vehicle_dht11_class。
	 */
	gpio_class = class_create(THIS_MODULE, "vehicle_dht11_class");

	/* 判断class_create是否返回错误指针。 */
	if (IS_ERR(gpio_class))

	/* 错误处理代码块开始。 */
	{

		/* 打印源码文件、函数名和行号。 */
		printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);

		/*
		 * class创建失败时，
		 * 注销前面已经注册的字符设备。
		 */
		unregister_chrdev(major, "vehicle_dht11");

		/* 返回class_create对应的真实错误码。 */
		return PTR_ERR(gpio_class);

	/* 错误处理代码块结束。 */
	}

	/*
	 * 在前面创建的class下创建设备。
	 * 设备节点名称为vehicle_dht11，
	 * 用户空间最终通过/dev/vehicle_dht11访问驱动。
	 */
	device_create(gpio_class, NULL, MKDEV(major, 0), NULL, "vehicle_dht11");

	/*
	 * 返回probe结果。
	 * 原代码返回的是前面gpio_request留下的err。
	 * 当前实机运行时err为0，所以probe成功。
	 */
	return err;

/* probe函数结束。 */
}

/*
 * 定义platform_driver的remove函数。
 * 当模块卸载或设备解绑时，内核会调用它。
 */
static int dht11_remove(struct platform_device *pdev)

/* 函数体开始。 */
{

	/* 循环变量。 */
    int i;

	/* 计算GPIO数组元素数量。 */
    int count = sizeof(gpios)/sizeof(gpios[0]);

	/* 打印当前源码文件、函数名和行号。 */
	printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);

	/*
	 * 删除通过device_create()创建的设备，
	 * 对应/dev/vehicle_dht11。
	 */
	device_destroy(gpio_class, MKDEV(major, 0));

	/* 销毁/sys/class/vehicle_dht11_class。 */
	class_destroy(gpio_class);

	/* 注销字符设备并释放主设备号。 */
	unregister_chrdev(major, "vehicle_dht11");

	/* 遍历所有GPIO项。当前只有一个。 */
	for (i = 0; i < count; i++)

	/* for循环体开始。 */
	{

		/*
		 * 下面的free_irq被注释。
		 * 因为正常read结束时已经调用free_irq。
		 */
		//free_irq(gpios[i].irq, &gpios[i]);

		/*
		 * 下面的del_timer被注释。
		 * 原代码没有在remove中主动删除timer。
		 */
		//del_timer(&gpios[i].key_timer);

	/* for循环体结束。 */
	}

	/* remove成功完成，返回0。 */
	return 0;

/* remove函数结束。 */
}

/*
 * 定义设备树匹配表。
 * compatible必须与设备树节点中的字符串完全一致。
 */
static const struct of_device_id dht11_of_match[] = {

	/*
	 * 当设备树节点包含
	 * compatible = "damon,vehicle-dht11"
	 * 时，该驱动可以与节点匹配。
	 */
	{ .compatible = "damon,vehicle-dht11" },

	/* 空元素表示匹配表结束。 */
	{ }

/* 设备树匹配表定义结束。 */
};

/*
 * 将设备树匹配表导出到模块信息中，
 * 便于内核模块自动匹配和加载。
 */
MODULE_DEVICE_TABLE(of, dht11_of_match);

/* 定义platform_driver对象。 */
static struct platform_driver dht11_platform_driver = {

	/* 设备匹配成功后调用dht11_probe。 */
	.probe = dht11_probe,

	/* 设备解绑或模块卸载时调用dht11_remove。 */
	.remove = dht11_remove,

	/* 初始化platform_driver内部的通用driver成员。 */
	.driver = {

		/* 设置驱动名称。 */
		.name = "vehicle_dht11",

		/* 指定该驱动使用的设备树匹配表。 */
		.of_match_table = dht11_of_match,

	/* 内部driver结构体初始化结束。 */
	},

/* platform_driver结构体初始化结束。 */
};

/*
 * 使用内核提供的宏一次性完成：
 * 模块加载时注册platform_driver；
 * 模块卸载时注销platform_driver。
 */
module_platform_driver(dht11_platform_driver);

/*
 * 声明模块采用GPL许可证。
 * 某些仅向GPL模块导出的内核符号要求该声明。
 */
MODULE_LICENSE("GPL");