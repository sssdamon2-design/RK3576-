#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/gpio/consumer.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/string.h>
/*
 * DHT11一次返回5字节，共40位数据。
 */
#define DHT11_DATA_BYTES              5
#define DHT11_DATA_BITS              40

/*
 * 主机发送启动信号时，将数据线拉低20毫秒；
 * 释放数据线后等待30微秒。
 */
#define DHT11_START_LOW_MS            20
#define DHT11_START_RELEASE_US        30

/*
 * 等待一次电平变化的最长时间。
 */
#define DHT11_LEVEL_TIMEOUT_US       120

/*
 * 高电平持续时间超过40微秒时，判断该数据位为1。
 */
#define DHT11_BIT_ONE_THRESHOLD_US    40

/*
 * 保存一个DHT11设备对应的驱动私有数据。
 *
 * 
 */
struct vehicle_dht11_data   //定义一个专门的结构体用于保存驱动私有数据结构体
{
    struct  gpio_desc *data_gpio;    //这是Linux GPIO子系统定义的一种结构体类型，保存GPIO的信息
    u8 raw[DHT11_DATA_BYTES];// u8表示无符号八位整数   定义数组保存5个u8 每个u8一字节 用于保存温湿度
    /*
     * 解析后的温度，单位为千分之一摄氏度。
     *
     * 例如：
     * 26℃保存为26000。
     */
    int temperature_milli_c;

    /*
     * 解析后的相对湿度，单位为千分之一%RH。
     *
     * 例如：
     * 63%RH保存为63000。
     */
    int humidity_milli_percent;
};



/*
 * 等待DHT11数据线变成指定电平。
 *
 * target_level：
 * 0表示等待低电平；
 * 1表示等待高电平。
 *
 * timeout_us：
 * 最多等待多少微秒。
 *
 * elapsed_us：
 * 用于返回实际等待了多少微秒。
 *
 * 返回值：
 * 0           等到了目标电平；
 * 负数错误码   GPIO读取失败或等待超时。
 */
static int vehicle_dht11_wait_for_level(
    struct vehicle_dht11_data *dht11,
    int target_level,
    unsigned int timeout_us,
    unsigned int *elapsed_us)
{
    unsigned int count;
    int value;

    for (count = 0; count < timeout_us; count++) {
        value = gpiod_get_value(dht11->data_gpio);
        if (value < 0)
            return value;

        if (value == target_level) {
            if (elapsed_us)
                *elapsed_us = count;

            return 0;
        }

        udelay(1);
    }

    if (elapsed_us)
        *elapsed_us = timeout_us;

    return -ETIMEDOUT;
}



/*
 * 向DHT11发送主机启动信号。
 *
 * 通信流程：
 * 1. 将数据线配置为输出低电平；
 * 2. 保持低电平20毫秒；
 * 3. 将数据线切换为输入，释放总线；
 * 4. 等待30微秒，让DHT11准备响应。
 *
 * 返回值：
 * 0           启动信号发送成功；
 * 负数错误码   GPIO方向切换失败。
 */
static int vehicle_dht11_send_start(
    struct vehicle_dht11_data *dht11)
{
    int ret;

    ret = gpiod_direction_output(dht11->data_gpio, 0);
    if (ret)
        return ret;

    msleep(DHT11_START_LOW_MS);

    ret = gpiod_direction_input(dht11->data_gpio);
    if (ret)
        return ret;

    udelay(DHT11_START_RELEASE_US);

    return 0;
}





/*
 * 检查DHT11的响应信号。
 *
 * 主机释放数据线后，DHT11应当依次输出：
 *
 * 1. 约80微秒低电平；
 * 2. 约80微秒高电平；
 * 3. 随后进入第一位数据的低电平阶段。
 *
 * 返回值：
 * 0             响应时序正常；
 * -ETIMEDOUT     等待电平变化超时；
 * 其他负数错误码 GPIO读取失败。
 */
static int vehicle_dht11_check_response(
    struct vehicle_dht11_data *dht11)
{
    unsigned int low_time_us;
    unsigned int high_time_us;
    int ret;

    /*
     * 等待DHT11开始输出响应低电平。
     */
    ret = vehicle_dht11_wait_for_level(
              dht11,
              0,
              DHT11_LEVEL_TIMEOUT_US,
              NULL);
    if (ret)
        return ret;

    /*
     * 当前已经进入DHT11响应低电平。
     * 等待数据线变高，并记录低电平持续时间。
     */
    ret = vehicle_dht11_wait_for_level(
              dht11,
              1,
              DHT11_LEVEL_TIMEOUT_US,
              &low_time_us);
    if (ret)
        return ret;

    /*
     * 当前已经进入DHT11响应高电平。
     * 等待数据线再次变低，并记录高电平持续时间。
     */
    ret = vehicle_dht11_wait_for_level(
              dht11,
              0,
              DHT11_LEVEL_TIMEOUT_US,
              &high_time_us);
    if (ret)
        return ret;

    return 0;
}

/*
 * 读取DHT11发送的一位数据。
 *
 * 调用本函数时，数据线应当处于当前数据位的起始低电平。
 *
 * 每一位数据的时序：
 * 1. 约50微秒低电平；
 * 2. 随后一段高电平；
 * 3. 高电平结束后进入下一位的低电平。
 *
 * DHT11通过高电平持续时间区分0和1：
 * 约26～28微秒表示0；
 * 约70微秒表示1。
 *
 * bit：
 * 用于返回解析得到的0或1。
 *
 * 返回值：
 * 0             读取成功；
 * -EINVAL       bit指针无效；
 * -ETIMEDOUT    等待电平变化超时；
 * 其他负数      GPIO读取失败。
 */
static int vehicle_dht11_read_bit(
    struct vehicle_dht11_data *dht11,
    u8 *bit)
{
    unsigned int high_time_us;
    int ret;

    if (!bit)
        return -EINVAL;

    /*
     * 当前处于数据位的起始低电平。
     * 等待数据线变成高电平，表示低电平阶段结束。
     */
    ret = vehicle_dht11_wait_for_level(
              dht11,
              1,
              DHT11_LEVEL_TIMEOUT_US,
              NULL);
    if (ret)
        return ret;

    /*
     * 当前已经进入高电平阶段。
     * 等待数据线重新变成低电平，并记录高电平持续时间。
     */
    ret = vehicle_dht11_wait_for_level(
              dht11,
              0,
              DHT11_LEVEL_TIMEOUT_US,
              &high_time_us);
    if (ret)
        return ret;

    /*
     * 根据高电平持续时间判断该位是0还是1。
     */
    if (high_time_us > DHT11_BIT_ONE_THRESHOLD_US)
        *bit = 1;
    else
        *bit = 0;

    return 0;
}


/*
 * 连续读取DHT11发送的40位数据。
 *
 * DHT11按照最高有效位在前的顺序发送数据。
 * 每读取8位，就组成一个完整字节。
 *
 * 最终数据保存到：
 *
 * raw[0]：湿度整数部分
 * raw[1]：湿度小数部分
 * raw[2]：温度整数部分
 * raw[3]：温度小数部分
 * raw[4]：校验和
 *
 * 返回值：
 * 0             读取成功；
 * 负数错误码     某一位读取失败。
 */
static int vehicle_dht11_read_bytes(
    struct vehicle_dht11_data *dht11)
{
    unsigned int i;
    unsigned int byte_index;
    u8 bit;
    int ret;

    /*
     * 清空上一次读取留下的原始数据。
     */
    memset(dht11->raw, 0, sizeof(dht11->raw));

    /*
     * DHT11一共发送40位数据。
     */
    for (i = 0; i < DHT11_DATA_BITS; i++) {
        ret = vehicle_dht11_read_bit(dht11, &bit);
        if (ret)
            return ret;

        /*
         * 每8位组成一个字节。
         *
         * i为0～7时，byte_index为0；
         * i为8～15时，byte_index为1；
         * 依次类推。
         */
        byte_index = i / 8;

        /*
         * DHT11先发送一个字节的最高位。
         *
         * 每收到一位：
         * 1. 将当前字节整体左移一位；
         * 2. 把新收到的bit放入最低位。
         */
        dht11->raw[byte_index] <<= 1;
        dht11->raw[byte_index] |= bit;
    }

    return 0;
}

/*
 * 检查DHT11返回数据的校验和。
 *
 * 校验规则：
 *
 * raw[0] + raw[1] + raw[2] + raw[3]
 *
 * 取计算结果的低8位，应当等于raw[4]。
 *
 * 返回值：
 * 0          校验成功；
 * -EBADMSG   校验失败，接收到的数据不可靠。
 */
static int vehicle_dht11_check_checksum(
    struct vehicle_dht11_data *dht11)
{
    unsigned int sum;

    sum = dht11->raw[0] +
          dht11->raw[1] +
          dht11->raw[2] +
          dht11->raw[3];

    if ((sum & 0xff) != dht11->raw[4])
        return -EBADMSG;

    return 0;
}







/*
 * 将通过校验的DHT11原始数据解析为温度和湿度。
 *
 * 当前经典DHT11版本按整数精度处理：
 *
 * raw[0]：湿度整数部分；
 * raw[2]：温度整数部分。
 *
 * 解析结果使用千分之一单位保存，
 * 避免在Linux内核中进行浮点运算。
 */
static void vehicle_dht11_parse_data(
    struct vehicle_dht11_data *dht11)
{
    dht11->humidity_milli_percent =
        dht11->raw[0] * 1000;

    dht11->temperature_milli_c =
        dht11->raw[2] * 1000;
}


/*
 * 完成一次完整的DHT11温湿度采样。
 *
 * 执行顺序：
 * 1. 向DHT11发送启动信号；
 * 2. 检查DHT11响应；
 * 3. 读取40位原始数据；
 * 4. 检查校验和；
 * 5. 解析温度和湿度。
 *
 * 返回值：
 * 0            采样成功；
 * 负数错误码    某个步骤执行失败。
 */
static int vehicle_dht11_read_sample(
    struct vehicle_dht11_data *dht11)
{
    int ret;

    /*
     * 发送主机启动信号：
     * 数据线拉低20毫秒，然后切换为输入。
     */
    ret = vehicle_dht11_send_start(dht11);
    if (ret)
        return ret;

    /*
     * 检查DHT11是否返回低—高响应信号。
     */
    ret = vehicle_dht11_check_response(dht11);
    if (ret)
        return ret;

    /*
     * 连续读取40位数据，并组成5个字节。
     */
    ret = vehicle_dht11_read_bytes(dht11);
    if (ret)
        return ret;

    /*
     * 检查前4个字节之和的低8位，
     * 是否等于第5个校验和字节。
     */
    ret = vehicle_dht11_check_checksum(dht11);
    if (ret)
        return ret;

    /*
     * 只有数据读取和校验全部成功后，
     * 才更新温度和湿度结果。
     */
    vehicle_dht11_parse_data(dht11);

    return 0;
}


static int vehicle_dht11_probe(struct platform_device *pdev)
{
    struct vehicle_dht11_data *dht11  ; // dht11用于保存驱动私有数据结构体的地址  还没有给实值
    int ret; //ret用于保存函数失败时的错误码
    dht11 = devm_kzalloc(&pdev->dev,sizeof(*dht11),GFP_KERNEL);
    //在内核空间分配一块足够容纳 struct vehicle_dht11_data 的内存，将内存全部清零，并把起始地址保存到 dht11 指针中。
    /* &pdev->dev 表示这块内存属于当前 DHT11 设备。
    devm 表示：device managed 由设备管理.  当设备与驱动解除绑定时，内核会自动释放这块内存。
    sizeof(*dht11) 计算struct vehicle_dht11_data 这个结构体需要占用多少字节。
    GFP_KERNEL表示：当前处于普通内核上下文，允许内核以正常方式分配内存，必要时可以等待。这里的 GFP 可以理解为内核的内存分配标志。
    在 probe() 中通常可以使用：GFP_KERNEL因为 probe() 不是中断处理函数，可以睡眠等待
返回值保存到 dht11
dht11 = devm_kzalloc(...);成功时：dht11指向一块有效内存
失败时：dht11 == NULL
因此必须检查。
    */
    if(!dht11)
        return -ENOMEM;
    //ENOMEM 表示：Error: No Memory内存不足内核错误码通常以负数形式返回，所以写成：-ENOMEM当 probe() 返回负数时，内核会认为设备初始化失败：
    //内存分配失败→ probe() 返回 -ENOMEM→ 设备和驱动绑定失败

    dht11->data_gpio = devm_gpiod_get(&pdev->dev,"data",GPIOD_IN);
//dht11->data_gpio 访问结构体中的 struct  gpio_desc *data_gpio。
//&pdev->dev 表示为当前 vehicle-dht11 平台设备获取 GPIO
//“data”表示查找设备树的data-gpios属性
//GPIOD_IN  表示申请成功后，将 GPIO 设置为输入方向。
//devm 表示自动管理生命
//devm_gpiod_get 函数返回struct gpio_desc *       也就是GPIO描述符的地址。

    if (IS_ERR(dht11->data_gpio))  //判断 devm_gpiod_get() 返回的是否是错误指针。返回的是一个布尔结果：返回0：不是错误指针
                                    //返回非0，通常表现为1：是错误指针。
    {
        ret = PTR_ERR (dht11->data_gpio); //从错误指针中取出具体的负数错误码。
        dev_err(&pdev->dev,"failed to get data GPIO:%d\n",ret); //表示以当前设备的名义打印错误日志。
        
        return ret;


    }

    platform_set_drvdata(pdev, dht11);//把 dht11 指针保存到当前 platform_device 的驱动私有数据位置中。存档地址。
//两个参数分别是：pdev当前匹配成功的 platform_device
//dht11 当前设备对应的私有数据结构体指针


    dev_info(&pdev->dev,
             "data GPIO acquired, starting initial sample\n");

    /*
     * 临时在probe()中执行一次完整采样，
     * 用于验证当前DHT11通信代码。
     */
    ret = vehicle_dht11_read_sample(dht11);
    if (ret) {
        dev_err(&pdev->dev,
                "initial DHT11 sample failed: %d\n",
                ret);

        return ret;
    }

    /*
     * 打印DHT11返回的5个原始字节，
     * 方便后续检查数据格式和校验和。
     */
    dev_info(&pdev->dev,
             "raw data: %u %u %u %u %u\n",
             (unsigned int)dht11->raw[0],
             (unsigned int)dht11->raw[1],
             (unsigned int)dht11->raw[2],
             (unsigned int)dht11->raw[3],
             (unsigned int)dht11->raw[4]);

    /*
     * 打印解析后的温度和湿度。
     */
    dev_info(&pdev->dev,
             "temperature=%d.%03d C, humidity=%d.%03d %%RH\n",
             dht11->temperature_milli_c / 1000,
             dht11->temperature_milli_c % 1000,
             dht11->humidity_milli_percent / 1000,
             dht11->humidity_milli_percent % 1000);











    dev_info(&pdev->dev, "vehicle DHT11 probe success\n"); //dev_info 是内核提供的打印函数，类似于printf
                                                        // pdev 是一个指针指向匹配成功的platform device，也就是匹配成功创建出一个平台设备，这里指向他
            //dev 是pdev指向的结构体即platform_device 内部的对象dev成员 &pdev->dev的作用是得到打印中的 vehicle-dht11 vehicle-dht11:
    return 0;
}


/*
 * 当驱动被卸载，或者设备与驱动解除绑定时调用。
 */
static int vehicle_dht11_remove(struct platform_device *pdev)
{
    dev_info(&pdev->dev, "vehicle DHT11 remove\n");

    return 0;
}

/*
 * 设备树匹配表。
 */
static const struct of_device_id vehicle_dht11_of_match[] = {
    { .compatible = "damon,vehicle-dht11" },
    { }
};

MODULE_DEVICE_TABLE(of, vehicle_dht11_of_match);

/*
 * platform驱动对象。
 */
static struct platform_driver vehicle_dht11_driver = {
    .probe = vehicle_dht11_probe,
    .remove = vehicle_dht11_remove,

    .driver = {
        .name = "vehicle-dht11",
        .of_match_table = vehicle_dht11_of_match,
    },
};

/*
 * 加载模块时注册platform_driver，
 * 卸载模块时注销platform_driver。
 */
module_platform_driver(vehicle_dht11_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Damon");
MODULE_DESCRIPTION("Platform driver for vehicle DHT11");