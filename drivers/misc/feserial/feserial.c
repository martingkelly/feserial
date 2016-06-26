#include <linux/fs.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/of.h>
#include <linux/serial_reg.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#define FESERIAL_NAME "feserial"

#define SERIAL_RESET_COUNTER (0)
#define SERIAL_GET_COUNTER (1)

struct feserial_dev {
	struct miscdevice miscdev;
	void __iomem *regs;
	unsigned int write_count;
};

unsigned int reg_read(struct feserial_dev *dev, int off) {
	return readl(dev->regs + 4*off);
}

void reg_write(struct feserial_dev *dev, unsigned int val, int off) {
	writel(val, dev->regs + 4*off);
}

void uart_write(struct feserial_dev *dev, char c) {
	while (!(reg_read(dev, UART_LSR) & UART_LSR_THRE))
		cpu_relax();

	reg_write(dev, c, UART_TX);
}


static ssize_t feserial_read(struct file *file, char __user *buf, size_t count,
	loff_t *offset) {
	return -EINVAL;
}

static ssize_t feserial_write(struct file *file, const char __user *buf,
	size_t count, loff_t *offset) {
	char c;
	struct feserial_dev *dev;
	int i;

	dev = container_of(file->private_data, struct feserial_dev, miscdev);
	for (i = 0; i < count; i++) {
		get_user(c, &buf[i]);
		uart_write(dev, c);
	}
	dev->write_count += count;

	return count;
}

static long feserial_ioctl(struct file *file, unsigned int cmd,
	unsigned long arg) {
	struct feserial_dev *dev;
	unsigned int __user *argp;

	dev = container_of(file->private_data, struct feserial_dev, miscdev);
	switch(cmd) {
	case SERIAL_RESET_COUNTER:
		dev->write_count = 0;
		break;
	case SERIAL_GET_COUNTER:
		argp = (unsigned int __user *) arg;
		put_user(dev->write_count, argp);
		break;
	default:
		return -ENXIO;
	}

	return 0;
}

static const struct file_operations fops = {
	.owner = THIS_MODULE,
	.read = feserial_read,
	.write = feserial_write,
	.unlocked_ioctl = feserial_ioctl
};


static int feserial_probe(struct platform_device *pdev)
{
	unsigned int baud_divisor;
	struct feserial_dev *dev;
	const char *name;
	struct resource *res;
	unsigned int uartclk;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		pr_err("Cannot get resource\n");
		return -ENODEV;
	}

	dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
	if (dev == NULL) {
		dev_err(&pdev->dev, "Cannot allocate dev\n");
		return -ENOMEM;
	}
	dev->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(dev->regs)) {
		dev_err(&pdev->dev, "Cannot remap registers\n");
		return PTR_ERR(dev->regs);
	}

	platform_set_drvdata(pdev, dev);

	pm_runtime_enable(&pdev->dev);
	pm_runtime_get_sync(&pdev->dev);

	/* Baud rate */
	of_property_read_u32(pdev->dev.of_node, "clock-frequency", &uartclk);
	baud_divisor = uartclk / 16 / 115200;
	reg_write(dev, 0x07, UART_OMAP_MDR1);
	reg_write(dev, 0x00, UART_LCR);
	reg_write(dev, UART_LCR_DLAB, UART_LCR);
	reg_write(dev, baud_divisor & 0xff, UART_DLL);
	reg_write(dev, (baud_divisor >> 8) & 0xff, UART_DLM);
	reg_write(dev, UART_LCR_WLEN8, UART_LCR);

	/* Soft reset */
	reg_write(dev, UART_FCR_CLEAR_RCVR | UART_FCR_CLEAR_XMIT, UART_FCR);
	reg_write(dev, 0x00, UART_OMAP_MDR1);

	dev->write_count = 0;

	/* Setup misc device. */
	dev->miscdev.minor = MISC_DYNAMIC_MINOR;
	name = kasprintf(GFP_KERNEL, FESERIAL_NAME "-%x", res->start);
	dev->miscdev.name = name;
	dev->miscdev.fops = &fops;
	misc_register(&dev->miscdev);

	return 0;
}

static int feserial_remove(struct platform_device *pdev)
{
	struct feserial_dev *dev;

	dev = platform_get_drvdata(pdev);
	misc_deregister(&dev->miscdev);
	kfree(dev->miscdev.name);
	pm_runtime_disable(&pdev->dev);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id feserial_of_match[] = {
		{ .compatible = "free-electrons,serial" },
		{}
};
MODULE_DEVICE_TABLE(of, feserial_of_match);
#endif

static struct platform_driver feserial_driver = {
	.driver = {
		.name = FESERIAL_NAME,
		.owner = THIS_MODULE,
		.of_match_table = feserial_of_match
	},
	.probe = feserial_probe,
	.remove = feserial_remove,
};

module_platform_driver(feserial_driver);
MODULE_LICENSE("GPL");
