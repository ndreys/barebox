
#include <common.h>
#include <errno.h>
#include <driver.h>
#include <malloc.h>
#include <init.h>
#include <linux/err.h>
#include <led.h>
#include <poller.h>

enum {
	BLINK_ENCODER_GAP    = 1000,
	BLINK_ENCODER_SPACE  = 5000,
	BLINK_ENCODER_ON_OFF = 500,
	BLINK_ENCODER_REPEAT_COUNT = 3,
};

#define BLINK_ENCODER_DIGITS_NUM	2
#define BLINK_ENCODER_PATTERN_LEN_MAX (BLINK_ENCODER_DIGITS_NUM * 9 * 2)

struct blink_code {
	struct list_head list;
	unsigned int pattern[BLINK_ENCODER_PATTERN_LEN_MAX];
	unsigned int pattern_len;
};

struct blink_encoder {
	char *name;
	struct cdev cdev;
	struct list_head codes;
	struct poller_struct poller;
	enum led_trigger trigger;
	struct blink_code *current;
	unsigned int repeat_counter;
};

static ssize_t blink_encoder_read(struct cdev *cdev, void *buf,
				  size_t count, loff_t offset,
				  unsigned long flags)
{
	return -ENOTSUPP;
}

static void
blink_encoder_code_to_pattern(unsigned int digits[BLINK_ENCODER_DIGITS_NUM],
			      unsigned int *pattern,
			      unsigned int *pattern_len)
{
	unsigned int i, k;

	for (k = 0, i = 0; k < BLINK_ENCODER_DIGITS_NUM; k++) {
		int j;
		
		for (j = 0; j < digits[k]; j++) {
			pattern[i++] = BLINK_ENCODER_ON_OFF; /* on */
			pattern[i++] = BLINK_ENCODER_ON_OFF; /* off */
		}

		/* override last "off" in a digit */
		pattern[i - 1] = (k == BLINK_ENCODER_DIGITS_NUM - 1) ?
			BLINK_ENCODER_SPACE : BLINK_ENCODER_GAP;

	}

	*pattern_len = i;
}

static ssize_t blink_encoder_write(struct cdev *cdev, const void *buf,
				   size_t count, loff_t offset,
				   unsigned long flags)
{
	struct blink_encoder *be = cdev->priv;
	struct blink_code *code;
	const uint8_t *c = buf;
	unsigned int digits[BLINK_ENCODER_DIGITS_NUM] = {
		[0] = *c >> 4,
		[1] = *c & 0xf,
	};
	int i;

	if (count > 1)
		return -EINVAL;

	if (offset)
		return -EINVAL;

	for (i = 0; i < BLINK_ENCODER_DIGITS_NUM; i++) {
		switch (digits[i]) {
		case 1 ... 9:
			break;
		default:
			return -EINVAL;
		}
	}

	for (i = 0; i < BLINK_ENCODER_REPEAT_COUNT; i++) {
		code = xzalloc(sizeof(*code));
		blink_encoder_code_to_pattern(digits, code->pattern,
					      &code->pattern_len);
		list_add_tail(&code->list, &be->codes);
	}

	be->repeat_counter = 10;

	return count;
}

static struct cdev_operations blink_encoder_ops = {
	.read  = blink_encoder_read,
	.write = blink_encoder_write,
};

static void blink_encoder_poller_func(struct poller_struct *poller)
{
	struct blink_encoder *be = container_of(poller, struct blink_encoder,
						poller);
	if (list_empty(&be->codes))
		return;

	if (led_trigger_patter_is_active(be->trigger))
		return;

	if (!be->repeat_counter)
		return;

	be->current = list_prepare_entry(be->current, &be->codes, list);
	list_for_each_entry_continue(be->current, &be->codes, list) {
		led_trigger_pattern_once(be->trigger, be->current->pattern,
					 be->current->pattern_len);
		break;
	}

	be->repeat_counter--;
}

static int blink_encoder_probe(struct device_d *dev)
{
	struct blink_encoder *be;
	const char *trigger_name;
	char *devname;
	int ret;

	trigger_name = of_get_property(dev->device_node,
				       "barebox,default-trigger", NULL);
	if (!trigger_name)
		return -EINVAL;
	
	be = xzalloc(sizeof(*be));

	be->trigger = trigger_by_name(trigger_name);
	if (be->trigger == LED_TRIGGER_MAX)
		return -EINVAL;
	
	devname = cdev_find_free_name_or_alias(dev->device_node,
					       "blink-encoder");
	if (IS_ERR(devname))
		return PTR_ERR(devname);

	be->cdev.size = 0;
	be->cdev.ops = &blink_encoder_ops;
	be->cdev.dev = dev;
	be->cdev.priv = be;
	be->cdev.name = devname;
	be->cdev.flags = DEVFS_IS_CHARACTER_DEV;
	INIT_LIST_HEAD(&be->codes);

	ret = devfs_create(&be->cdev);
	if (ret)
		return ret;

	be->poller.func = blink_encoder_poller_func;

	return poller_register(&be->poller);
}

static __maybe_unused struct of_device_id blink_encoder_dt_ids[] = {
	{
		.compatible = "barebox,blink-encoder",
	}, {
		/* sentinel */
	},
};

static struct driver_d blink_encoder_driver = {
	.name = "blink-encoder",
	.probe = blink_encoder_probe,
	.of_compatible = blink_encoder_dt_ids,
};
device_platform_driver(blink_encoder_driver);
