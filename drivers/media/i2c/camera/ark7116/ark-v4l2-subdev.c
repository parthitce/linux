#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/export.h>
#include <media/soc_camera.h>
#include <linux/videodev2.h>
#include <media/v4l2-chip-ident.h>

#include "module_diff.h"

bool light_on;

static const struct regval_list module_svga_regs[] = {
	ENDMARKER,
};

/*
 * window size list
 */
static struct camera_module_win_size module_win_bt656_P = {
	.name = "BT656",
	.width = 720,
	.height = 576,
	.win_regs = module_svga_regs,
	.frame_rate_array = frame_rate_bt656,
	.capture_only = 0,
};

static struct camera_module_win_size module_win_bt656_N = {
	.name = "BT656",
	.width = 720,
	.height = 480,
	.win_regs = module_svga_regs,
	.frame_rate_array = frame_rate_bt656,
	.capture_only = 0,
};
static struct camera_module_win_size *module_win_list[] = {
	&module_win_bt656_P,
	&module_win_bt656_N,
};

static struct v4l2_ctl_cmd_info v4l2_ctl_array[] = {
	{
	 .id = V4L2_CID_GAIN,
	 .min = 256,
	 .max = 0XFFFF,
	 .step = 1,
	 .def = 2560,
	 },
	{
	 .id = V4L2_CID_AUTO_WHITE_BALANCE,
	 .min = 0,
	 .max = 1,
	 .step = 1,
	 .def = 1,
	 },
	{
	 .id = V4L2_CID_WHITE_BALANCE_TEMPERATURE,
	 .min = 0,
	 .max = 3,
	 .step = 1,
	 .def = 1,
	 },
	{
	 .id = V4L2_CID_SENSOR_ID,
	 .min = 0,
	 .max = 0xffffff,
	 .step = 1,
	 .def = 0,
	 },
};

static struct v4l2_ctl_cmd_info_menu v4l2_ctl_array_menu[] = {
	{
	 .id = V4L2_CID_COLORFX,
	 .max = 3,
	 .mask = 0x0,
	 .def = 0,
	 },
	{
	 .id = V4L2_CID_EXPOSURE_AUTO,
	 .max = 1,
	 .mask = 0x0,
	 .def = 1,
	 },
};
/*
 * supported color format list.
 * see definition in
 * http://thread.gmane.org/gmane.linux.drivers.video-input-infrastructure/
 * 12830/focus=13394
 * YUYV8_2X8_LE == YUYV with LE packing
 * YUYV8_2X8_BE == UYVY with LE packing
 * YVYU8_2X8_LE == YVYU with LE packing
 * YVYU8_2X8_BE == VYUY with LE packing
 */
static const struct module_color_format module_cfmts[] = {
	{
	 .code = V4L2_MBUS_FMT_UYVY8_2X8,
	 .colorspace = V4L2_COLORSPACE_JPEG,
	 },
};



static int module_start_aec(struct v4l2_subdev *sd)
{
	int ret = 0;
	return ret;
}

static int module_freeze_aec(struct v4l2_subdev *sd)
{
	int ret = 0;
	return ret;
}

static int module_save_exposure_param(struct v4l2_subdev *sd)
{
	int ret = 0;
	return ret;
}

static int module_set_exposure_param(struct v4l2_subdev *sd)
{
	int ret = 0;
	return ret;
}

static int module_set_exposure_auto(struct v4l2_subdev *sd,
				    struct v4l2_ctrl *ctrl)
{
	int ret = 0;
	return ret;
}

static int module_set_auto_white_balance(struct v4l2_subdev *sd,
					 struct v4l2_ctrl *ctrl)
{
	int ret = 0;
	return ret;
}

static int module_set_white_balance_temperature(struct v4l2_subdev *sd,
						struct v4l2_ctrl *ctrl)
{
	int ret = 0;
	return ret;
}

static int module_set_colorfx(struct v4l2_subdev *sd, struct v4l2_ctrl *ctrl)
{
	unsigned int ret = 0;
	return ret;
}

static int module_set_scene_exposure(struct v4l2_subdev *sd,
				     struct v4l2_ctrl *ctrl)
{
	int ret = 0;
	return ret;
}

static int module_set_stream(struct i2c_client *client, int enable)
{
	struct camera_module_priv *priv = to_camera_priv(client);
	struct i2c_adapter *i2c_adap = client->adapter;
	int ret = 0;

	if (!enable) {
		DBG_INFO("stream down");
		return ret;
	}

	if (NULL == priv->win || NULL == priv->cfmt) {
		DBG_ERR("cfmt or win select error");
		return -EPERM;
	}
	DBG_INFO("stream on");
	return 0;
}

static int module_set_exposure(struct i2c_client *client, int val)
{
	int ret = 0;
	return ret;
}

static int module_get_exposure(struct i2c_client *client, int *val)
{
	int ret = 0;
	DBG_INFO(" val = 0x%04x", val);
	return ret;
}

static int module_get_gain(struct i2c_client *client, int *val)
{
	int ret = 0;

	DBG_INFO(" val = 0x%04x", val);
	return ret;
}

static int module_set_gain(struct i2c_client *client, int val)
{
	int ret = 0;
	return ret;
}

static int module_set_ev(struct v4l2_subdev *sd, struct v4l2_ctrl *ctrl)
{
	int ret = 0;
	return ret;
}

static int module_set_mbusformat(struct i2c_client *client,
				 const struct module_color_format *cfmt)
{
	return 0;
}

static int module_s_mirror_flip(struct v4l2_subdev *sd, struct v4l2_ctrl *ctrl)
{
	return 0;
}

static int module_verify_pid(struct i2c_adapter *i2c_adap,
			     struct camera_module_priv *priv)
{
	return 0;
}

static int module_set_af_mode(struct v4l2_subdev *sd, struct v4l2_ctrl *ctrl)
{
	return 0;
}

static int module_get_af_mode(struct v4l2_subdev *sd, struct v4l2_ctrl *ctrl)
{
	int ret = 0;
	ctrl->val = NONE_AF;
	return ret;
}

static int module_get_af_status(struct camera_module_priv *priv,
				struct v4l2_ctrl *ctrl)
{
	return 0;
}

static void update_after_init(struct i2c_adapter *i2c_adap)
{
	int ret;
	unsigned int reg_0xfe;
	ret = camera_i2c_read(i2c_adap, 0xb2, 1, 0x26, &reg_0xfe);
	//printk("reg_0xfe 0x%x\n", reg_0xfe);
}

static void enter_preview_mode(struct i2c_adapter *i2c_adap)
{
}

static void enter_capture_mode(struct i2c_adapter *i2c_adap)
{
}

static int module_set_mirror_flip(struct i2c_client *client, int mf)
{
	return 0;
}

static int module_set_color_format(int mf)
{
	return 0;
}

static int module_set_af_region(struct v4l2_subdev *sd, struct v4l2_ctrl *ctrl)
{
	return 0;
}

static int module_set_power_line(struct v4l2_subdev *sd, struct v4l2_ctrl *ctrl)
{
	return 0;
}

static int module_get_power_line(struct v4l2_subdev *sd, struct v4l2_ctrl *ctrl)
{
	ctrl->val = V4L2_CID_POWER_LINE_FREQUENCY_AUTO;
	return 0;
}

static int get_sensor_id(struct v4l2_subdev *sd, struct v4l2_ctrl *ctrl)
{
	int ret = 0;

	return ret;
}




static struct camera_module_win_size *camera_module_select_win(struct i2c_adapter *i2c_adap, u32 width,
							       u32 height)
{
	unsigned int diff = 0;
	struct camera_module_win_size *win = NULL;
	int win_num = 0;
	int i = 0;
	int j = 0;
	int temp = 0;

	//DBG_INFO("width:%d, height:%d", width, height);
	win_num = ARRAY_SIZE(module_win_list);
	pr_info("%s, [%d x %d]:win_num=%d.\n", __func__,width,height,win_num);
	if (win_num == 1) {
		win = module_win_list[0];
		j = 0;
	} else {
#if 0
		diff = abs(width - module_win_list[0]->width) +
		    abs(height - module_win_list[0]->height);
		win = module_win_list[0];
		j = 0;
		for (i = 1; i < win_num; i++) {
			if (diff > abs(width - module_win_list[i]->width) +
			    abs(height - module_win_list[i]->height)) {
				win = module_win_list[i];
				j = i;
				diff = abs(width - module_win_list[i]->width) +
				    abs(height - module_win_list[i]->height);
			}
		}
#else
		camera_i2c_read(i2c_adap, 0xb2, 1, 0x28, &temp);
		pr_info("temp=0x%x.\n",temp);
		if(temp & 0x04){ // P
			win = module_win_list[0];
			pr_info("select P .\n");
		}else{		 // N
			win = module_win_list[1];
			pr_info("select N .\n");
		}
#endif
	}

	return win;
}

static int camera_module_get_params(struct i2c_client *client,
				    u32 *width, u32 *height)
{
	struct camera_module_priv *priv = to_camera_priv(client);
	int ret = 0;
	pr_info("%s\n", __func__);
	if (priv->info->flags & SENSOR_FLAG_CHANNEL0)
		module_set_color_format(3 % 10);
	else
		module_set_color_format(3 / 10);

	/*
	 * select format
	 */
	priv->cfmt = module_cfmts;

	if (!priv->cfmt) {
		DBG_INFO("Unsupported sensor format.");
		goto module_get_fmt_error;
	}
	priv->win = camera_module_select_win(client->adapter , *width, *height);
	*width = priv->win->width;
	*height = priv->win->height;
	DBG_INFO("current params: %s %dX%d\n", priv->win->name, *width,
		 *height);
	return ret;

 module_get_fmt_error:
	priv->win = NULL;
	priv->cfmt = NULL;
	return -1;
}

static int camera_module_set_params(struct i2c_client *client, u32 *width,
				    u32 *height, enum v4l2_mbus_pixelcode code)
{
	struct camera_module_priv *priv = to_camera_priv(client);
	int ret = 0;
	int i = 0;
	pr_info("%s\n", __func__);
	/*
	 * select format
	 */
	priv->cfmt = NULL;
	for (i = 0; i < ARRAY_SIZE(module_cfmts); i++) {
		if (code == module_cfmts[i].code) {
			priv->cfmt = module_cfmts + i;
			break;
		}
	}

	if (!priv->cfmt) {
		DBG_INFO("Unsupported sensor format.");
		goto module_set_fmt_error;
	}

	/*
	 * select win
	 */
	priv->win = camera_module_select_win(client->adapter, *width, *height);
	DBG_INFO("the window name is %s", priv->win->name);
	if (ret < 0)
		return ret;

	if (priv->info->flags & SENSOR_FLAG_CHANNEL0)
		module_set_mirror_flip(client, 3 % 10);
	else
		module_set_mirror_flip(client, 3 / 10);

	ret = module_set_mbusformat(client, priv->cfmt);

	if (ret < 0) {
		DBG_ERR("module set mbus format error.");
		goto module_set_fmt_error;
	}

	*width = priv->win->width;
	*height = priv->win->height;

	return ret;

 module_set_fmt_error:
	priv->win = NULL;
	priv->cfmt = NULL;

	return ret;
}

static int camera_module_set_flash_led_mode(struct v4l2_subdev *sd,
					    struct v4l2_ctrl *ctrl)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct camera_module_priv *priv = to_camera_priv(client);
	int mode = ctrl->val;

	switch (mode) {
	case V4L2_FLASH_LED_MODE_NONE:
		DBG_INFO("----V4L2_FLASH_LED_MODE_NONE %d",
		       V4L2_FLASH_LED_MODE_NONE);
		priv->flash_led_mode = mode;
		flashlight_control(FLASHLIGHT_OFF);
		break;

	case V4L2_FLASH_LED_MODE_TORCH:
		DBG_INFO("----V4L2_FLASH_LED_MODE_TORCH %d",
		       V4L2_FLASH_LED_MODE_TORCH);
		priv->flash_led_mode = mode;
		flashlight_control(FLASHLIGHT_TORCH);
		break;

	case V4L2_FLASH_LED_MODE_FLASH:
		DBG_INFO("----V4L2_FLASH_LED_MODE_FLASH %d",
		       V4L2_FLASH_LED_MODE_FLASH);
		priv->flash_led_mode = mode;
		break;

	case V4L2_FLASH_LED_MODE_AUTO:
		DBG_INFO("----V4L2_FLASH_LED_MODE_AUTO %d",
		       V4L2_FLASH_LED_MODE_AUTO);
		priv->flash_led_mode = mode;
		break;

	default:
		return -ERANGE;
	}

	ctrl->cur.val = mode;

	return 0;
}

/*
 * v4l2_subdev_core_ops function
 */
static int camera_module_g_chip_ident(struct v4l2_subdev *sd,
				      struct v4l2_dbg_chip_ident *id)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct camera_module_priv *priv = to_camera_priv(client);

	pr_info("%s\n", __func__);

	id->ident = priv->model;
	id->revision = 0;

	return 0;
}

/*
 * v4l2_ctrl_ops function
 */
static int camera_module_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct camera_module_priv *priv = container_of(ctrl->handler,
						       struct
						       camera_module_priv, hdl);
	struct v4l2_subdev *sd = &priv->subdev;
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	int ret = 0;
	pr_info("%s\n", __func__);

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		module_set_exposure(client, ctrl->val);
		break;

	case V4L2_CID_GAIN:
		module_set_gain(client, ctrl->val);
		break;

	case V4L2_CID_AUTO_WHITE_BALANCE:
		module_set_auto_white_balance(sd, ctrl);
		break;

	case V4L2_CID_WHITE_BALANCE_TEMPERATURE:
		module_set_white_balance_temperature(sd, ctrl);
		break;
	case V4L2_CID_HFLIP:
	case V4L2_CID_VFLIP:
		module_s_mirror_flip(sd, ctrl);
		break;

	case V4L2_CID_COLORFX:
		module_set_colorfx(sd, ctrl);
		break;

	case V4L2_CID_EXPOSURE_AUTO:
		module_set_exposure_auto(sd, ctrl);
		break;
	case V4L2_CID_SCENE_MODE:
		module_set_scene_exposure(sd, ctrl);
		break;

	case V4L2_CID_AF_MODE:
		module_set_af_mode(sd, ctrl);
		break;

	case V4L2_CID_AF_STATUS:
		break;
	case V4L2_CID_AF_REGION:
		module_set_af_region(sd, ctrl);
		break;
#ifdef CAMERA_MODULE_WITH_MOTOR
	case V4L2_CID_MOTOR:
		motor_set_pos(sd, ctrl);
		break;
#endif
	case V4L2_CID_FLASH_LED_MODE:
		if (priv->info->flags & SENSOR_FLAG_CHANNEL0)
			camera_module_set_flash_led_mode(sd, ctrl);
		break;

	case V4L2_CID_FLASH_STROBE:
		if (priv->info->flags & SENSOR_FLAG_CHANNEL0) {
			flashlight_control(FLASHLIGHT_TORCH);
			light_on = true;
		}
		break;

	case V4L2_CID_FLASH_STROBE_STOP:
		if (priv->info->flags & SENSOR_FLAG_CHANNEL0) {
			flashlight_control(TORCHLIGHT_OFF);
			light_on = false;
		}
		break;

	case V4L2_CID_EXPOSURE_COMP:
		module_set_ev(sd, ctrl);
		break;

	case V4L2_CID_POWER_LINE_FREQUENCY:
		module_set_power_line(sd, ctrl);
		break;
	default:
		DBG_INFO("v4l2-ctrl set %s, %d out of range",
			 v4l2_ctrl_get_name(ctrl->id), ctrl->val);
		return 0;
	}

	return ret;
}

static int camera_module_g_volatile_ctrl(struct v4l2_ctrl *ctrl)
{
	struct camera_module_priv *priv = container_of(ctrl->handler,
						       struct camera_module_priv, hdl);
	struct v4l2_subdev *sd = &priv->subdev;
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	int ret = 0;
	pr_info("%s\n", __func__);

	switch (ctrl->id) {
	case V4L2_CID_AF_STATUS:
		ret = module_get_af_status(priv, ctrl);
		break;

	case V4L2_CID_GAIN:
		ret = module_get_gain(client, &ctrl->val);
		break;

	case V4L2_CID_EXPOSURE:
		ret = module_get_exposure(client, &ctrl->val);
		break;

	case V4L2_CID_AF_MODE:
		module_get_af_mode(sd, ctrl);
		break;

	case V4L2_CID_SENSOR_ID:
		get_sensor_id(sd, ctrl);
		break;
	case V4L2_CID_POWER_LINE_FREQUENCY:
		ret = module_get_power_line(sd, ctrl);
		break;
	default:
		DBG_ERR("v4l2-ctrl get %s out of range",
			v4l2_ctrl_get_name(ctrl->id));
		return 0;
	}
	return 0;
}

/*
 * v4l2_subdev_video_ops function
 */
static int camera_module_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct camera_module_priv *priv = to_camera_priv(client);
	int ret = 0;
	pr_info("%s\n", __func__);

	if (enable && (ACTS_PREVIEW_MODE == priv->pcv_mode)) {
		DBG_INFO("ACTS_PREVIEW_MODE....");
		enter_preview_mode(client->adapter);
	}
	if (enable && (ACTS_CAPTURE_MODE == priv->pcv_mode)) {
		DBG_INFO("ACTS_CAPTURE_MODE....");
		enter_capture_mode(client->adapter);
	}
	if (enable) {
		if (light_on)
			flashlight_control(FLASHLIGHT_FLASH);
	} else {
		if (light_on)
			flashlight_control(FLASHLIGHT_OFF);
	}
	ret = module_set_stream(client, enable);

	if (ACTS_PREVIEW_MODE == priv->pcv_mode) {
		ret |= module_save_exposure_param(sd);
	} else if (ACTS_CAPTURE_MODE == priv->pcv_mode) {
		ret |= module_freeze_aec(sd);
		ret |= module_set_exposure_param(sd);
		ret |= module_start_aec(sd);
	}

	return ret;
}

static int camera_module_cropcap(struct v4l2_subdev *sd, struct v4l2_cropcap *a)
{
	pr_info("%s\n", __func__);
	a->bounds.left = 0;
	a->bounds.top = 0;
	a->bounds.width = MODULE_MAX_WIDTH;
	a->bounds.height = MODULE_MAX_HEIGHT;
	a->defrect = a->bounds;
	a->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	a->pixelaspect.numerator = 1;
	a->pixelaspect.denominator = 30;

	return 0;
}

static int camera_module_g_crop(struct v4l2_subdev *sd, struct v4l2_crop *vc)
{
	pr_info("%s\n", __func__);
	vc->c.left = 0;
	vc->c.top = 0;
	vc->c.width = MODULE_MAX_WIDTH;
	vc->c.height = MODULE_MAX_HEIGHT;
	vc->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	return 0;
}

static int camera_module_g_parm(struct v4l2_subdev *sd,
				struct v4l2_streamparm *parms)
{
	pr_info("%s\n", __func__);
	return 0;
}

static int camera_module_s_parm(struct v4l2_subdev *sd,
				struct v4l2_streamparm *parms)
{
	pr_info("%s\n", __func__);
	return 0;
}

static int camera_module_enum_framesizes(struct v4l2_subdev *sd,
					 struct v4l2_frmsizeenum *fsize)
{
	pr_info("%s\n", __func__);
	if (fsize->index >= ARRAY_SIZE(module_win_list))
		return -EINVAL;

	switch (fsize->pixel_format) {
	case V4L2_PIX_FMT_YUV420:
	case V4L2_PIX_FMT_YUV422P:
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_YUYV:
		fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
		fsize->discrete.width = module_win_list[fsize->index]->width;
		fsize->discrete.height = module_win_list[fsize->index]->height;
		fsize->reserved[0] =
		    module_win_list[fsize->index]->capture_only;
		break;

	default:
		fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
		fsize->discrete.width = module_win_list[fsize->index]->width;
		fsize->discrete.height = module_win_list[fsize->index]->height;
		fsize->reserved[0] =
		    module_win_list[fsize->index]->capture_only;
		break;
	}

	return 0;
}

static int camera_module_enum_frameintervals(struct v4l2_subdev *sd,
					     struct v4l2_frmivalenum *fival)
{
	pr_info("%s\n", __func__);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	const struct camera_module_win_size *win_size =
	    camera_module_select_win(client->adapter,fival->width, fival->height);
	unsigned int array_size = sizeof(win_size->frame_rate_array) /
	    sizeof(unsigned int);

	if (fival->index >= array_size)
		return -EINVAL;

	if ((win_size->width != fival->width) ||
	    (win_size->height != fival->height)) {
		DBG_INFO("width(%d) height(%d) is over range.",
		       fival->width, fival->height);
		return -EINVAL;
	}

	fival->type = V4L2_FRMIVAL_TYPE_DISCRETE;
	fival->discrete.numerator = 1;
	fival->discrete.denominator =
	    (win_size->frame_rate_array)[fival->index];

	return 0;
}

static int camera_module_enum_fmt(struct v4l2_subdev *sd, unsigned int index,
				  enum v4l2_mbus_pixelcode *code)
{
	pr_info("%s\n", __func__);
	if (index >= ARRAY_SIZE(module_cfmts))
		return -EINVAL;

	*code = module_cfmts[index].code;

	return 0;
}

static int camera_module_g_fmt(struct v4l2_subdev *sd,
			       struct v4l2_mbus_framefmt *mf)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct camera_module_priv *priv = to_camera_priv(client);
	int ret = 0;
	pr_info("%s\n", __func__);

	if (!priv->win || !priv->cfmt) {
		u32 width = MODULE_DEFAULT_WIDTH;
		u32 height = MODULE_DEFAULT_HEIGHT;

		ret = camera_module_get_params(client, &width, &height);
		if (ret < 0)
			return ret;
	}

	mf->width = priv->win->width;
	mf->height = priv->win->height;
	mf->code = priv->cfmt->code;
	mf->colorspace = priv->cfmt->colorspace;
	mf->field = V4L2_FIELD_NONE;

	return ret;
}

static int camera_module_try_fmt(struct v4l2_subdev *sd,
				 struct v4l2_mbus_framefmt *mf)
{
	pr_info("%s,width x height=%d x %d.\n", __func__,mf->width,mf->height);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct camera_module_priv *priv = to_camera_priv(client);
	const struct camera_module_win_size *win;
	int i = 0;

	int temp =100;
	camera_i2c_read(client->adapter, 0xb2, 1, 0x28, &temp);
	pr_info("temp=0x%x.\n,temp");

	/*select suitable win */
	win = camera_module_select_win(client->adapter,mf->width, mf->height);

	mf->width = win->width;
	mf->height = win->height;
	mf->field = V4L2_FIELD_NONE;

	for (i = 0; i < ARRAY_SIZE(module_cfmts); i++) {
		if (mf->code == module_cfmts[i].code)
			break;
	}

	if (i == ARRAY_SIZE(module_cfmts)) {
		/* Unsupported format requested. Propose either */
		if (priv->cfmt) {
			/* the current one or */
			mf->colorspace = priv->cfmt->colorspace;
			mf->code = priv->cfmt->code;
		} else {
			/* the default one */
			mf->colorspace = module_cfmts[0].colorspace;
			mf->code = module_cfmts[0].code;
		}
	} else {
		/* Also return the colorspace */
		mf->colorspace = module_cfmts[i].colorspace;
	}

	return 0;
}

static int camera_module_s_fmt(struct v4l2_subdev *sd,
			       struct v4l2_mbus_framefmt *mf)
{
	pr_info("%s\n", __func__);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct camera_module_priv *priv = to_camera_priv(client);
	int ret = 0;
	int temp =100;
	camera_i2c_read(client->adapter, 0xb2, 1, 0x28, &temp);
	pr_info("temp=0x%x.\n,temp");

	pr_info("[%s] width=%d, height=%d.\n", __func__,mf->width,mf->height);
	ret = camera_module_set_params(client, &mf->width, &mf->height,
				       mf->code);
	if (!ret)
		mf->colorspace = priv->cfmt->colorspace;

	return ret;
}

static int camera_module_g_mbus_config(struct v4l2_subdev *sd,
				       struct v4l2_mbus_config *cfg)
{
	pr_info("%s\n", __func__);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct camera_module_priv *priv = to_camera_priv(client);
	
	struct soc_camera_subdev_desc *desc = soc_camera_i2c_to_desc(client);

	switch ((priv->info->flags & SENSOR_FLAG_INTF_MASK) >> 1) {
	case 0:
		cfg->type = V4L2_MBUS_CSI2;
		cfg->flags = 1 << (priv->info->mipi_cfg->lan_num) |
		    V4L2_MBUS_CSI2_NONCONTINUOUS_CLOCK;
		break;
	case 1:
		cfg->type = V4L2_MBUS_PARALLEL;
		cfg->flags = DEFAULT_PCLK_SAMPLE_EDGE | V4L2_MBUS_MASTER |
		    V4L2_MBUS_HSYNC_ACTIVE_HIGH | DEFAULT_VSYNC_ACTIVE_LEVEL |
		    V4L2_MBUS_DATA_ACTIVE_HIGH;
		break;
	default:
		cfg->type = V4L2_MBUS_PARALLEL;
		cfg->flags = DEFAULT_PCLK_SAMPLE_EDGE | V4L2_MBUS_MASTER |
		    V4L2_MBUS_HSYNC_ACTIVE_HIGH | DEFAULT_VSYNC_ACTIVE_LEVEL |
		    V4L2_MBUS_DATA_ACTIVE_HIGH;
		break;
	}

	cfg->flags = soc_camera_apply_board_flags(desc, cfg);

	return 0;
}

static int camera_module_s_mbus_config(struct v4l2_subdev *sd,
				       const struct v4l2_mbus_config *cfg)
{
	pr_info("%s\n", __func__);
	return 0;
}

static long camera_module_ioctrl(struct v4l2_subdev *sd,
				 unsigned int cmd, void *arg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct camera_module_priv *priv = to_camera_priv(client);
	pr_info("%s\n", __func__);

	switch (cmd) {
	case V4L2_CID_CAM_CV_MODE:{
			int mode = *(int *)arg;

			if (mode < ACTS_PREVIEW_MODE
				|| mode > ACTS_VIDEO_MODE) {
				return -EINVAL;
			}
			priv->pcv_mode = mode;
			break;
		}
	default:
		DBG_ERR("Don't support current cmd:0x%x", cmd);
		return -EINVAL;
	}

	switch (priv->pcv_mode) {
	case ACTS_PREVIEW_MODE:
		DBG_INFO("Preview Mode\n");
		break;
	case ACTS_CAPTURE_MODE:
		DBG_INFO("Capture Mode\n");
		break;
	case ACTS_VIDEO_MODE:
		DBG_INFO("Video Mode\n");
		break;
	default:
		DBG_ERR("out of range");
		break;
	}

	return 0;
}

static int camera_module_s_power(struct v4l2_subdev *sd, int on)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct soc_camera_subdev_desc *desc = soc_camera_i2c_to_desc(client);
	int ret = 0;

	pr_info("%s", on ? "on" : "off");

	return ret;
}

static int camera_module_g_skip_frames(struct v4l2_subdev *sd, u32 *frames)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct camera_module_priv *priv = to_camera_priv(client);
	pr_info("%s\n", __func__);

	if (ACTS_CAPTURE_MODE == priv->pcv_mode)
		*frames = DROP_NUM_CAPTURE;
	else
		*frames = DROP_NUM_PREVIEW;

	DBG_INFO("skip %d frames.", *frames);
	return 0;
}

static const struct v4l2_ctrl_ops camera_module_ctrl_ops = {
	.g_volatile_ctrl = camera_module_g_volatile_ctrl,
	.s_ctrl = camera_module_s_ctrl,
};

static struct v4l2_subdev_sensor_ops module_subdev_sensor_ops = {
	.g_skip_frames = camera_module_g_skip_frames,
};

static struct v4l2_subdev_core_ops camera_module_subdev_core_ops = {
	.g_chip_ident = camera_module_g_chip_ident,
	.ioctl = camera_module_ioctrl,
	.s_power = camera_module_s_power,
};

static struct v4l2_subdev_video_ops camera_module_subdev_video_ops = {
	.s_stream = camera_module_s_stream,
	.cropcap = camera_module_cropcap,
	.g_crop = camera_module_g_crop,
	.g_parm = camera_module_g_parm,
	.s_parm = camera_module_s_parm,
	.enum_framesizes = camera_module_enum_framesizes,
	.enum_frameintervals = camera_module_enum_frameintervals,
	.enum_mbus_fmt = camera_module_enum_fmt,
	.g_mbus_fmt = camera_module_g_fmt,
	.try_mbus_fmt = camera_module_try_fmt,
	.s_mbus_fmt = camera_module_s_fmt,
	.g_mbus_config = camera_module_g_mbus_config,
	.s_mbus_config = camera_module_s_mbus_config,
};

static struct v4l2_subdev_ops module_subdev_ops = {
	.core = &camera_module_subdev_core_ops,
	.video = &camera_module_subdev_video_ops,
	.sensor = &module_subdev_sensor_ops,
};

static void camera_module_init_ops(struct v4l2_ctrl_handler *hdl,
				   const struct v4l2_ctrl_ops *ops)
{
	unsigned int cmd_array_size = ARRAY_SIZE(v4l2_ctl_array);
	unsigned int cmd_menu_array_size = ARRAY_SIZE(v4l2_ctl_array_menu);

	struct v4l2_ctrl *ret = NULL;
	unsigned int i = 0;

	v4l2_ctrl_handler_init(hdl, cmd_array_size + cmd_menu_array_size);

	for (i = 0; i < cmd_array_size; i++) {
		const struct v4l2_ctl_cmd_info *pctl = v4l2_ctl_array + i;

		ret = v4l2_ctrl_new_std(hdl, ops, pctl->id, pctl->min,
					pctl->max, pctl->step, pctl->def);

		if (NULL == ret) {
			DBG_ERR
			    ("ctr[%d] - id:%d, min:%d, max:%d, step:%d, def:%d",
			     i, pctl->id, pctl->min, pctl->max, pctl->step,
			     pctl->def);
		}
		if ((pctl->id == V4L2_CID_GAIN)
		    || (pctl->id == V4L2_CID_POWER_LINE_FREQUENCY)
		    || (pctl->id == V4L2_CID_AF_STATUS)
		    || (pctl->id == V4L2_CID_AF_MODE)
		    || (pctl->id == V4L2_CID_EXPOSURE)
		    || (pctl->id == V4L2_CID_MOTOR)
		    || (pctl->id == V4L2_CID_MOTOR_GET_MAX)
		    || (pctl->id == V4L2_CID_AF_REGION)
		    || (pctl->id == V4L2_CID_SENSOR_ID)) {
			if (ret != NULL) {
				ret->flags |= V4L2_CTRL_FLAG_VOLATILE;
				ret = NULL;
			}
		}

		hdl->error = 0;
	}
	for (i = 0; i < cmd_menu_array_size; i++) {
		const struct v4l2_ctl_cmd_info_menu *pmenu =
		    v4l2_ctl_array_menu + i;
		if (pmenu->id == V4L2_CID_FLASH_LED_MODE)
			continue;

		ret = v4l2_ctrl_new_std_menu(hdl, ops, pmenu->id, pmenu->max,
					     pmenu->mask, pmenu->def);

		if (NULL == ret) {
			DBG_ERR("menu[%d] - id:%d, max:%d, mask:%d, def:%d", i,
				pmenu->id, pmenu->max, pmenu->mask, pmenu->def);
		}
		hdl->error = 0;
	}
	return;
}

int module_register_v4l2_subdev(struct camera_module_priv *priv,
				struct i2c_client *client)
{
	int ret;
	pr_info("%s\n", __func__);

	v4l2_i2c_subdev_init(&priv->subdev, client, &module_subdev_ops);
	camera_module_init_ops(&priv->hdl, &camera_module_ctrl_ops);

	priv->subdev.ctrl_handler = &priv->hdl;
	priv->hdl.error = 0;
	if (priv->hdl.error) {
		ret = priv->hdl.error;
		kfree(priv);
		pr_err("module init error!");
		return ret;
	}

	priv->pcv_mode = ACTS_PREVIEW_MODE;
	ret = v4l2_ctrl_handler_setup(&priv->hdl);

	return 0;
}
EXPORT_SYMBOL(module_register_v4l2_subdev);
