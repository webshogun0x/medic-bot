# Custom Avatar Guide

## Option 1: Using the Current Drawn Avatar (Implemented)
The current implementation creates a circular avatar with initials that automatically updates when you change the name.

**Features:**
- Circular blue background with white border
- Shows first letter of first name and first letter of last name
- Updates automatically when name changes
- No external image files needed

## Option 2: Creating Your Own Image Avatar

### Step 1: Prepare Your Image
1. Create or find an avatar image (80x80 pixels recommended)
2. Save as PNG format
3. Name it `custom_avatar.png`

### Step 2: Convert Image to C Array
1. Use LVGL's online image converter: https://lvgl.io/tools/imageconverter
2. Upload your `custom_avatar.png`
3. Set color format to `CF_TRUE_COLOR_ALPHA` for transparency support
4. Set output format to `C array`
5. Download the generated `.c` file

### Step 3: Add to Project
1. Place the generated `.c` file in `main/assets/` folder
2. Add the image declaration to your header file:
```c
LV_IMAGE_DECLARE(custom_avatar);
```

### Step 4: Update Code
Replace the drawn avatar code with:
```c
// Custom Avatar Image
LV_IMAGE_DECLARE(custom_avatar);
lv_obj_t * avatar = lv_img_create(overview_panel);
lv_img_set_src(avatar, &custom_avatar);
lv_obj_align(avatar, LV_ALIGN_TOP_MID, 0, 10);
```

## Current BMI Data Ranges
The BMI chart now shows varying data across different BMI categories:
- **Underweight**: < 18.5 (18.5)
- **Normal**: 18.5-24.9 (19.7, 20.3, 21.4, 22.9, 23.1, 24.2, 24.8)
- **Overweight**: 25-29.9 (25.6, 26.8, 27.3, 28.1)
- **Obese**: ≥ 30 (none in current data)

This provides a realistic representation of BMI fluctuations over time.