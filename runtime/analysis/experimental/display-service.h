struct a7m4_display_state {
    unsigned selection, mode, updates, panel_updates, brightness_updates;
    unsigned output_mask, mask_updates, layout_updates, plane_updates, disconnected;
    unsigned hdmi_mode, hdmi_updates, monitor_lut, lut_updates;
    unsigned osd_flags[3], osd_updates, monitor_size, font_size, event_queue;
    unsigned char event_settings[16], display_option[12];
    unsigned hdmi_fixed_option, option_updates, hdmi_format_updates;
    unsigned char hdmi_format[8], frame_rates[5];
    unsigned frame_rate_updates, panel_reverse_updates;
    unsigned char panel_reverse[20];
    unsigned char panel[14], brightness[6], layouts[8][32], planes[18][32];
};
int a7m4_display_reply(struct a7m4_display_state *, const void *, unsigned, void *, unsigned *);
