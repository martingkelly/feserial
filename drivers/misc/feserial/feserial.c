#include <linux/init.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/of.h>
#include <linux/serial_reg.h>

struct feserial_dev {
	void __iomem *regs;
};

unsigned int reg_read(struct feserial_dev *dev, int off) {
	return readl(dev->regs + 4*off);
}

void reg_write(struct feserial_dev *dev, unsigned int val, int off) {
	writel(val, dev->regs + 4*off);
}

void test_write_char(struct feserial_dev *dev, char c) {
	while (!(reg_read(dev, UART_LSR) & UART_LSR_THRE))
		cpu_relax();

	reg_write(dev, c, UART_TX);
}

static int feserial_probe(struct platform_device *pdev)
{
	unsigned int baud_divisor;
	struct feserial_dev *dev;
	struct resource *res;
	unsigned int uartclk;

	pr_info("Called feserial_probe\n");

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

	test_write_char(dev, 'a');
	test_write_char(dev, 'b');
	test_write_char(dev, 'c');
	test_write_char(dev, 'd');

	return 0;
}

static int feserial_remove(struct platform_device *pdev)
{
	pr_info("Called feserial_remove\n");

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
		.name = "feserial",
		.owner = THIS_MODULE,
		.of_match_table = feserial_of_match
	},
	.probe = feserial_probe,
	.remove = feserial_remove,
};

module_platform_driver(feserial_driver);
MODULE_LICENSE("GPL");
