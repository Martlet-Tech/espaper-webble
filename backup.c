int is_valid_jpg_filename(const char *filename)
{
	const char *dot = strrchr(filename, '.');
	if (dot == NULL || strcmp(dot, ".jpg") != 0) {
		return 0;
	}

	// 去掉后缀部分
	size_t len = dot - filename;
	char base_name[len + 1];
	strncpy(base_name, filename, len);
	base_name[len] = '\0';

	// 检查 base_name 是否为正整数
	char *endptr;
	long num = strtol(base_name, &endptr, 10);
	return (*endptr == '\0' && endptr != base_name && num > 0);
}

int bsp_get_jpg_count()
{
	const char mount_point[] = "/sdcard";
	DIR *dir = opendir(mount_point);
	int count = 0;

	if (dir == NULL) {
		printf("Failed to open directory: %s\n", mount_point);
		return -1;
	}

	struct dirent *entry;
	while ((entry = readdir(dir)) != NULL) {
		if (is_valid_jpg_filename(entry->d_name)) {
			count++;
		}
	}

	closedir(dir);
	return count;
}

int *bsp_get_jpg_numbers(int *size)
{
	const char mount_point[] = "/sdcard";
	DIR *dir = opendir(mount_point);
	if (dir == NULL) {
		printf("Failed to open directory: %s\n", mount_point);
		*size = 0;
		return NULL;
	}

	// 第一次遍历统计数量
	int count = 0;
	struct dirent *entry;
	while ((entry = readdir(dir)) != NULL) {
		if (is_valid_jpg_filename(entry->d_name)) {
			count++;
		}
	}
	closedir(dir);

	if (count == 0) {
		*size = 0;
		return NULL;
	}

	int *numbers = (int *)malloc(count * sizeof(int));
	if (numbers == NULL) {
		printf("Memory allocation failed\n");
		*size = 0;
		return NULL;
	}

	// 第二次遍历提取整数
	dir = opendir(mount_point);
	count = 0;
	while ((entry = readdir(dir)) != NULL) {
		if (is_valid_jpg_filename(entry->d_name)) {
			char *endptr;
			long num = strtol(entry->d_name, &endptr, 10);
			numbers[count++] = (int)num;
		}
	}
	closedir(dir);

	*size = count;
	return numbers;
}

int bsp_get_max_jpg_number()
{
	const char mount_point[] = "/sdcard";
	DIR *dir = opendir(mount_point);
	int max_num = -1; // 如果没有找到有效的 JPG 文件，返回 -1

	if (dir == NULL) {
		printf("Failed to open directory: %s\n", mount_point);
		return -1;
	}

	struct dirent *entry;
	while ((entry = readdir(dir)) != NULL) {
		if (is_valid_jpg_filename(entry->d_name)) {
			const char *dot = strrchr(entry->d_name, '.');
			size_t len = dot - entry->d_name;

			char base_name[len + 1];
			strncpy(base_name, entry->d_name, len);
			base_name[len] = '\0';

			char *endptr;
			long num = strtol(base_name, &endptr, 10);

			if (*endptr == '\0' && endptr != base_name && num > 0) {
				if (num > max_num) {
					max_num = num;
				}
			}
		}
	}

	closedir(dir);
	return max_num;
}

void cleanup(int *numbers)
{
	if (numbers) {
		free(numbers);
	}
}

// 使用静态链表，不需要释放
// 扫描 SD 卡并构建链表
void scan_sdcard_and_build_list(Node **jpg_list)
{
	DIR *dir = opendir(SDCARD_MOUNT_POINT);
	if (dir == NULL) {
		printf("Failed to open directory: %s\n", SDCARD_MOUNT_POINT);
		return;
	}

	struct dirent *entry;

	while ((entry = readdir(dir)) != NULL) {
		if (is_valid_jpg_filename(entry->d_name)) {
			const char *dot = strrchr(entry->d_name, '.');
			size_t len = dot - entry->d_name;

			char base_name[len + 1];
			strncpy(base_name, entry->d_name, len);
			base_name[len] = '\0';

			char *endptr;
			long num = strtol(base_name, &endptr, 10);

			if (*endptr == '\0' && endptr != base_name && num > 0) {
				insert_node_sorted(jpg_list, (int)num);
			}
		}
	}

	closedir(dir);
}

void show_current(Node *current)
{
	if (current == NULL) {
		printf("No images to display.\n");
		return;
	}
	printf("Displaying image: %d.jpg\n", current->value);
}

// 加载SD卡中的图片文件列表
void load_image_list()
{
	DIR *dir = opendir("/sdcard");
	if (!dir) {
		printf("Failed to open SD card directory.\n");
		return;
	}

	struct dirent *entry;

	image_count = 0; // 重置图片计数器
	while ((entry = readdir(dir)) != NULL) {
		// 检查文件名是否以 ".jpg" 结尾
		if (strstr(entry->d_name, ".jpg")) {
			strncpy(image_files[image_count], entry->d_name,
				sizeof(image_files[image_count]) - 1);
			image_files[image_count]
				   [sizeof(image_files[image_count]) - 1] =
					   '\0'; // 确保字符串以 '\0' 结尾
			image_count++;

			if (image_count >= MAX_IMAGES) {
				printf("Image limit reached: %d\n", MAX_IMAGES);
				break;
			}
		}
	}

	closedir(dir);
}
