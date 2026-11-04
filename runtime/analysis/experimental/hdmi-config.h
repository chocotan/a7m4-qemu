struct a7m4_hdmi_config {
    unsigned format, premium_photo, format_updates, photo_updates;
};
int a7m4_hdmi_config_reply(struct a7m4_hdmi_config *, const void *, unsigned, void *);
