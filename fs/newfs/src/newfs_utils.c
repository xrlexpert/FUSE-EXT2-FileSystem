#include "../include/newfs.h"

extern struct newfs_super      newfs_super; 
extern struct custom_options   newfs_options;

/**
 * @brief 驱动读
 * 
 * @param offset 
 * @param out_content 
 * @param size 
 * @return int 
 */
int newfs_driver_read(int offset, uint8_t *out_content, int size) {
    int      offset_aligned = NEWFS_ROUND_DOWN(offset, NEWFS_IO_SZ());
    int      bias           = offset - offset_aligned;
    int      size_aligned   = NEWFS_ROUND_UP((size + bias), NEWFS_IO_SZ());
    uint8_t* temp_content   = (uint8_t*)malloc(size_aligned);
    uint8_t* cur            = temp_content;
    // lseek(NEWFS_DRIVER(), offset_aligned, SEEK_SET);
    ddriver_seek(NEWFS_DRIVER(), offset_aligned, SEEK_SET);
    while (size_aligned != 0)
    {
        // read(NEWFS_DRIVER(), cur, NEWFS_IO_SZ());
        ddriver_read(NEWFS_DRIVER(), cur, NEWFS_IO_SZ());
        cur          += NEWFS_IO_SZ();
        size_aligned -= NEWFS_IO_SZ();   
    }
    memcpy(out_content, temp_content + bias, size);
    free(temp_content);
    return NEWFS_ERROR_NONE;
}
int newfs_driver_write(int offset, uint8_t *in_content, int size) {
    int      offset_aligned = NEWFS_ROUND_DOWN(offset, NEWFS_IO_SZ());
    int      bias           = offset - offset_aligned;
    int      size_aligned   = NEWFS_ROUND_UP((size + bias), NEWFS_IO_SZ());
    uint8_t* temp_content   = (uint8_t*)malloc(size_aligned);
    uint8_t* cur            = temp_content;
    newfs_driver_read(offset_aligned, temp_content, size_aligned);
    memcpy(temp_content + bias, in_content, size);
    
    // lseek(NEWFS_DRIVER(), offset_aligned, SEEK_SET);
    ddriver_seek(NEWFS_DRIVER(), offset_aligned, SEEK_SET);
    while (size_aligned != 0)
    {
        // write(NEWFS_DRIVER(), cur, NEWFS_IO_SZ());
        ddriver_write(NEWFS_DRIVER(), cur, NEWFS_IO_SZ());
        cur          += NEWFS_IO_SZ();
        size_aligned -= NEWFS_IO_SZ();   
    }

    free(temp_content);
    return NEWFS_ERROR_NONE;
}
/**
 * @brief 将denry插入到其父目录绑定的inode中，采用头插法
 * 
 * @param inode 
 * @param dentry 
 * @return int 
 */
int newfs_alloc_dentry(struct newfs_inode* inode, struct newfs_dentry* dentry, boolean judge) {
    if (inode->dentrys == NULL) {
        inode->dentrys = dentry;
    }
    else {
        dentry->brother = inode->dentrys;          //原来的链表头目录变为当前目录的兄弟
        inode->dentrys = dentry;
    }
    inode->dir_cnt++;
    if(judge){
        /* 检查位图是否有空位 */
        if((inode->dir_cnt % NEWFS_DENTRYS_PER_BLK) == 1){ //需要找到新的逻辑块来存
            int byte_cursor = 0; 
            int bit_cursor  = 0; 
            int data_cursor  = 0;
            boolean is_find_free_entry = FALSE;
            for (byte_cursor = 0; byte_cursor < NEWFS_BLKS_SZ(newfs_super.data_map_blks); //字节位
                byte_cursor++)
            {
                for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++) {          //比特位
                    if((newfs_super.data_map[byte_cursor] & (0x1 << bit_cursor)) == 0) {    
                                                            /* 当前data_cursor位置空闲 */
                        newfs_super.data_map[byte_cursor] |= (0x1 << bit_cursor);
                        is_find_free_entry = TRUE;           
                        break;
                    }
                    data_cursor++;
                }
                if (is_find_free_entry) {
                    break;
                }
            }
            if (!is_find_free_entry || data_cursor == newfs_super.ino_max)
                return -NEWFS_ERROR_NOSPACE;
            /*这里只是为了记录数据块是否被占用*/
            int cur_blk = inode->dir_cnt / NEWFS_DENTRYS_PER_BLK;
            inode->blk_pointers[cur_blk] = data_cursor;
        }

    }
    return inode->dir_cnt;
}

/**
 * @brief 
 * 
 * @param dentry dentry指向ino，读取该inode
 * @param ino inode唯一编号
 * @return struct newfs_inode* 
 */
struct newfs_inode* newfs_read_inode(struct newfs_dentry * dentry, int ino) {
    struct newfs_inode* inode = (struct newfs_inode*)malloc(sizeof(struct newfs_inode));
    struct newfs_inode_d inode_d;
    struct newfs_dentry* sub_dentry;
    struct newfs_dentry_d dentry_d;
    int    dir_cnt = 0, i;
    /* 从磁盘读索引结点 */
    if (newfs_driver_read(NEWFS_INO_OFS(ino), (uint8_t *)&inode_d, 
                        sizeof(struct newfs_inode_d)) != NEWFS_ERROR_NONE) {
        NEWFS_DBG("[%s] io error\n", __func__);
        return NULL;                    
    }
    inode->dir_cnt = 0;
    inode->ino = inode_d.ino;
    inode->allocated_nums = inode_d.allocated_nums;
    inode->size = inode_d.size;
    memcpy(inode->target_path, inode_d.target_path, NEWFS_MAX_FILE_NAME);
    inode->dentry = dentry;
    inode->dentrys = NULL;
    for(int i = 0;i <NEWFS_DATA_PER_FILE; i++){
        inode->blk_pointers[i] = inode_d.blk_pointers[i];
    }
    /* 内存中的inode的数据或子目录项部分也需要读出 */
    if (NEWFS_IS_DIR(inode)) {
        printf("READ INODE\n");
        printf("root ino:%d, name%s\n", inode->ino, dentry->fname);
        printf("root inode offset:%d\n", NEWFS_INO_OFS(ino));
        dir_cnt = inode_d.dir_cnt;
        int blk_num = 0;
        int offset;
        while(dir_cnt > 0 && blk_num < NEWFS_DATA_PER_FILE){
            offset = NEWFS_DATA_OFS(inode->blk_pointers[blk_num]);
            printf("    origin offset:%d\n", offset);
            while(dir_cnt > 0 && offset + sizeof(struct newfs_dentry_d) < NEWFS_DATA_OFS(inode->blk_pointers[blk_num] + 1))
            {
                if (newfs_driver_read(offset, (uint8_t *)&dentry_d, sizeof(struct newfs_dentry_d)) != NEWFS_ERROR_NONE) {
                    NEWFS_DBG("[%s] io error\n", __func__);
                    return NULL;
                }
                sub_dentry = new_dentry(dentry_d.fname, dentry_d.ftype);
                sub_dentry->parent = inode->dentry;
                sub_dentry->ino    = dentry_d.ino; 
                printf("    child ino:%d, child_fname%s, child dentry offset:%d\n",dentry_d.ino, dentry_d.fname, offset);
                newfs_alloc_dentry(inode, sub_dentry, FALSE); //读的时候不需要判断是否需要额外分配逻辑块给dentry
                dir_cnt --;
                offset += sizeof(struct newfs_dentry_d);
            }
            blk_num +=1;
        }
    }
    else if (NEWFS_IS_REG(inode)) {
        for(int i = 0;i <NEWFS_DATA_PER_FILE; i++){
            inode->data = (uint8_t *)malloc(NEWFS_BLKS_SZ(NEWFS_DATA_PER_FILE));
            if(inode->blk_pointers[i] == -1) continue; //如果尚未分配，直接跳过
            if (newfs_driver_read(NEWFS_DATA_OFS(inode->blk_pointers[i]), (uint8_t *)inode->data + i * NEWFS_BLK_SZ(), NEWFS_BLK_SZ()) != NEWFS_ERROR_NONE) {
                NEWFS_DBG("[%s] io error\n", __func__);
                return NULL;                    
            }
            printf("    read file:%s, offset:%d\n", inode->dentry->fname, NEWFS_DATA_OFS(inode->blk_pointers[i]));
        }
    }
    return inode;
}
/**
 * @brief 分配一个inode，占用位图
 * 
 * @param dentry 该dentry指向分配的inode
 * @return newfs_inode
 */
struct newfs_inode* newfs_alloc_inode(struct newfs_dentry * dentry) {
    struct newfs_inode* inode;
    int byte_cursor = 0; 
    int bit_cursor  = 0; 
    int ino_cursor  = 0;
    boolean is_find_free_entry = FALSE;
    /* 检查位图是否有空位 */
    for (byte_cursor = 0; byte_cursor < NEWFS_BLKS_SZ(newfs_super.ino_map_blks); //字节位
         byte_cursor++)
    {
        for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++) {          //比特位
            if((newfs_super.ino_map[byte_cursor] & (0x1 << bit_cursor)) == 0) {    
                                                      /* 当前ino_cursor位置空闲 */
                newfs_super.ino_map[byte_cursor] |= (0x1 << bit_cursor);
                is_find_free_entry = TRUE;           
                break;
            }
            ino_cursor++;
        }
        if (is_find_free_entry) {
            break;
        }
    }
    if (!is_find_free_entry || ino_cursor == newfs_super.ino_max){
        printf("分配失败！！\n");
        return -NEWFS_ERROR_NOSPACE;
    }

    inode = (struct newfs_inode*)malloc(sizeof(struct newfs_inode));
    inode->ino  = ino_cursor; 
    inode->size = 0;
    inode->allocated_nums = 0;
                                                      /* dentry指向inode */
    dentry->inode = inode;
    dentry->ino   = inode->ino;
                                                      /* inode指回dentry */
    inode->dentry = dentry;
    inode->dir_cnt = 0;
    inode->dentrys = NULL;
    
    if (NEWFS_IS_REG(inode)) {
        for(int i = 0;i < NEWFS_DATA_PER_FILE; i++){
            inode->blk_pointers[i] = -1;           //采用动态分配，初始化-1
            inode->data = (uint8_t *)malloc(NEWFS_BLKS_SZ(NEWFS_DATA_PER_FILE));
        }
    }
    return inode;
}

/**
 * @brief 将内存inode及其下方结构全部刷回磁盘
 * 
 * @param inode 
 * @return int 
 */
int newfs_sync_inode(struct newfs_inode * inode) {
    struct newfs_inode_d  inode_d;
    struct newfs_dentry*  dentry_cursor;
    struct newfs_dentry_d dentry_d;
    int ino             = inode->ino;
    inode_d.ino         = ino;
    inode_d.size        = inode->size;
    inode_d.allocated_nums = inode->allocated_nums;
    memcpy(inode_d.target_path, inode->target_path, NEWFS_MAX_FILE_NAME);
    inode_d.ftype       = inode->dentry->ftype;
    inode_d.dir_cnt     = inode->dir_cnt;
    int offset;
    for(int i=0; i< NEWFS_DATA_PER_FILE; i++){
        inode_d.blk_pointers[i] = inode->blk_pointers[i];
    }
    /* 先写inode本身 */
    if (newfs_driver_write(NEWFS_INO_OFS(ino), (uint8_t *)&inode_d, 
                     sizeof(struct newfs_inode_d)) != NEWFS_ERROR_NONE) {
        NEWFS_DBG("[%s] io error\n", __func__);
        return -NEWFS_ERROR_IO;
    }
    /* 再写inode下方的数据 */
    if (NEWFS_IS_DIR(inode)) { /* 如果当前inode是目录，那么数据是目录项，且目录项的inode也要写回 */    
        printf("START WRITE TO DISK\n");
        printf("root ino:%d\n", ino);
        printf("root inode offset:%d\n", NEWFS_INO_OFS(ino));
        printf("root dir_cnt:%d\n",inode_d.dir_cnt);
        printf("root fname:%s\n", inode->dentry->fname);
        printf("root type:%d\n", inode->dentry->ftype);     
        int blk_num = 0;                 
        dentry_cursor = inode->dentrys;
        while(dentry_cursor != NULL && blk_num < NEWFS_DATA_PER_FILE){
            offset = NEWFS_DATA_OFS(inode->blk_pointers[blk_num]);
            printf("    origin offset:%d\n", offset);
            while (dentry_cursor != NULL && offset + sizeof(struct newfs_dentry_d) < NEWFS_DATA_OFS(inode->blk_pointers[blk_num] + 1))
            {
                memcpy(dentry_d.fname, dentry_cursor->fname, NEWFS_MAX_FILE_NAME);
                dentry_d.ftype = dentry_cursor->ftype;
                dentry_d.ino = dentry_cursor->ino;
                printf("    child ino:%d, child fname %s, child dentry offset:%d\n",dentry_d.ino, dentry_d.fname, offset);
                if (newfs_driver_write(offset, (uint8_t *)&dentry_d, 
                                    sizeof(struct newfs_dentry_d)) != NEWFS_ERROR_NONE) {
                    NEWFS_DBG("[%s] io error\n", __func__);
                    return -NEWFS_ERROR_IO;                     
                }
                
                if (dentry_cursor->inode != NULL) {
                    newfs_sync_inode(dentry_cursor->inode);
                }

                dentry_cursor = dentry_cursor->brother;
                offset += sizeof(struct newfs_dentry_d);
            }
            blk_num += 1;
        }
    }
    else if (NEWFS_IS_REG(inode)) { /* 如果当前inode是文件，那么数据是文件内容，直接写即可 */
        for(int i = 0;i < NEWFS_DATA_PER_FILE; i++){
            if(inode->blk_pointers[i] == -1) continue; //如果尚未分配，直接跳过
            if (newfs_driver_write(NEWFS_DATA_OFS(inode->blk_pointers[i]), inode->data + i * NEWFS_BLK_SZ(), NEWFS_BLK_SZ()) != NEWFS_ERROR_NONE) {
                NEWFS_DBG("[%s] io error\n", __func__);
                return -NEWFS_ERROR_IO;
            }
            printf("    write file:%s, offset:%d\n", inode->dentry->fname, NEWFS_DATA_OFS(inode->blk_pointers[i]));

        }
    }
    return NEWFS_ERROR_NONE;
}

int  newfs_mount(struct custom_options options){
    int                 ret = NEWFS_ERROR_NONE;
    int                 driver_fd;
    struct newfs_super_d  newfs_super_d; 
    struct newfs_dentry*  root_dentry;
    struct newfs_inode*   root_inode;
    int                 inode_num;
    int                 inode_blks;
    int                 ino_map_blks;
    
    int                 super_blks;
    boolean             is_init = FALSE;

    newfs_super.is_mounted = FALSE;

	driver_fd = ddriver_open(newfs_options.device);
	 if (driver_fd < 0) {
        return driver_fd;
    }
    newfs_super.driver_fd = driver_fd;

    ddriver_ioctl(NEWFS_DRIVER(), IOC_REQ_DEVICE_SIZE, &newfs_super.disk_size);
	ddriver_ioctl(NEWFS_DRIVER(), IOC_REQ_DEVICE_IO_SZ, &newfs_super.io_size);
	newfs_super.blk_size = 2 * NEWFS_IO_SZ();

	root_dentry = new_dentry("/", NEWFS_DIR);

	if(newfs_driver_read(NEWFS_SUPER_OFS,  (uint8_t *)(&newfs_super_d), sizeof(struct newfs_super_d)) != NEWFS_ERROR_NONE){
		return -NEWFS_ERROR_IO;
	}

    printf("d_magic:%d\n", newfs_super_d.magic_num);
    printf("%d\n",newfs_super_d.data_offset);
    printf("START READ FROM DISK\n");
	if(newfs_super_d.magic_num != NEWFS_MAGIC_NUM){
		super_blks = NEWFS_ROUND_UP(sizeof(struct newfs_super_d), NEWFS_BLK_SZ())/ NEWFS_BLK_SZ();
		inode_num = NEWFS_DISK_SZ()/((NEWFS_DATA_PER_FILE + NEWFS_INODE_PER_FILE) * NEWFS_BLK_SZ()); 

        inode_blks = NEWFS_ROUND_UP(inode_num, NEWFS_INODES_PER_BLK) /NEWFS_INODES_PER_BLK;
		ino_map_blks = NEWFS_ROUND_UP(NEWFS_ROUND_UP(inode_blks, UINT32_BITS), NEWFS_BLK_SZ())/NEWFS_BLK_SZ(); 
		
		newfs_super_d.sb_offset = NEWFS_SUPER_OFS;
		newfs_super_d.sb_blks = super_blks;

		newfs_super_d.ino_map_offset = NEWFS_SUPER_OFS + NEWFS_BLKS_SZ(newfs_super_d.sb_blks);
		newfs_super_d.ino_map_blks = ino_map_blks;

		newfs_super_d.data_map_offset = newfs_super_d.ino_map_offset + NEWFS_BLKS_SZ(newfs_super_d.ino_map_blks);
		newfs_super_d.data_map_blks = 1;

		newfs_super_d.ino_offset = newfs_super_d.data_map_offset + NEWFS_BLKS_SZ(newfs_super_d.data_map_blks);
		newfs_super_d.ino_blks = inode_blks;

        newfs_super.ino_max = newfs_super_d.ino_blks;

		newfs_super_d.data_offset =newfs_super_d.ino_offset + NEWFS_BLKS_SZ(newfs_super_d.ino_blks);
		newfs_super_d.data_blks = NEWFS_ROUND_DOWN(NEWFS_DISK_SZ(), NEWFS_BLK_SZ())/NEWFS_BLK_SZ() - newfs_super_d.sb_blks - newfs_super_d.ino_map_blks - newfs_super_d.data_map_blks - newfs_super_d.ino_blks;
		newfs_super_d.usage_size = 0;

        newfs_super.data_max =  newfs_super_d.data_blks;
		NEWFS_DBG("inode map blocks: %d\n", ino_map_blks);
        NEWFS_DBG("disk_size: %d\n", newfs_super_d.disk_size);
        NEWFS_DBG("inode_num: %d\n", inode_num);
        NEWFS_DBG("inode_blks: %d\n", newfs_super_d.ino_blks);
        NEWFS_DBG("ino_map_blks: %d\n", newfs_super_d.ino_map_blks);
        NEWFS_DBG("ino_map_offset: %d\n", newfs_super_d.ino_map_offset);
        NEWFS_DBG("data_map_offset: %d\n", newfs_super_d.data_map_offset);
        NEWFS_DBG("ino_offset: %d\n", newfs_super_d.ino_offset);
        NEWFS_DBG("data_offset: %d\n", newfs_super_d.data_offset);
		is_init = TRUE;
	}
	newfs_super.usage_size = newfs_super_d.usage_size;
    /*超级块建立*/
    newfs_super.sb_blks = newfs_super_d.sb_blks;
    newfs_super.sb_offset = newfs_super_d.sb_offset;

    /*索引位图建立*/
    newfs_super.ino_map_blks = newfs_super_d.ino_map_blks;
	newfs_super.ino_map_offset = newfs_super_d.ino_map_offset;
	newfs_super.ino_map = (uint8_t *)malloc(NEWFS_BLKS_SZ(newfs_super_d.ino_map_blks));
    /*索引节点建立*/
    newfs_super.ino_blks = newfs_super_d.ino_blks;
    newfs_super.ino_offset = newfs_super_d.ino_offset;
    /*数据位图建立*/
    newfs_super.data_map_blks = newfs_super_d.data_map_blks;
    newfs_super.data_map_offset = newfs_super_d.data_map_offset;
    newfs_super.data_map = (uint8_t *)malloc(NEWFS_BLKS_SZ(newfs_super_d.data_map_blks));
    /*数据块建立*/
    newfs_super.data_blks =  newfs_super_d.data_blks;
	newfs_super.data_offset = newfs_super_d.data_offset;

	// newfs_dump_map();

	printf("\n--------------------------------------------------------------------------------\n\n");
	
	if (newfs_driver_read(newfs_super_d.ino_map_offset, (uint8_t *)(newfs_super.ino_map), 
                        NEWFS_BLKS_SZ(newfs_super_d.ino_map_blks)) != NEWFS_ERROR_NONE) {
        return -NEWFS_ERROR_IO;
    }

    if (newfs_driver_read(newfs_super_d.data_map_offset, (uint8_t *)(newfs_super.data_map), 
                        NEWFS_BLKS_SZ(newfs_super_d.data_map_blks)) != NEWFS_ERROR_NONE) {
        return -NEWFS_ERROR_IO;
    }

    if (is_init) {                                    /* 分配根节点 */
        root_inode = newfs_alloc_inode(root_dentry);
        newfs_sync_inode(root_inode);
    }
    
    root_inode            = newfs_read_inode(root_dentry, NEWFS_ROOT_INO);  /* 读取根目录 */
    root_dentry->inode    = root_inode;
    newfs_super.root_dentry = root_dentry;
    newfs_super.is_mounted  = TRUE;
    printf("FINISHED READING\n");

    // newfs_dump_map();
    return ret;
}
/**
 * @brief 
 * 
 * @return int 
 */
int newfs_umount() {
    struct newfs_super_d  newfs_super_d; 

    if (!newfs_super.is_mounted) {
        return NEWFS_ERROR_NONE;
    }
    printf("START UNMOUNT!!!\n");

    newfs_sync_inode(newfs_super.root_dentry->inode);     /* 从根节点向下刷写节点 */
                                                    
    newfs_super_d.magic_num          = NEWFS_MAGIC_NUM;
    newfs_super_d.usage_size = newfs_super.usage_size;
    /*超级块信息写回*/
    newfs_super_d.sb_blks = newfs_super.sb_blks;
    newfs_super_d.sb_offset = newfs_super.sb_offset;

    /*索引位图信息写回*/
    newfs_super_d.ino_map_blks = newfs_super.ino_map_blks;
	newfs_super_d.ino_map_offset = newfs_super.ino_map_offset;
	
    /*索引节点信息写回*/
    newfs_super_d.ino_blks = newfs_super.ino_blks;
    newfs_super_d.ino_offset = newfs_super.ino_offset;
    /*数据位图信息写回*/
    newfs_super_d.data_map_blks = newfs_super.data_map_blks;
    newfs_super_d.data_map_offset = newfs_super.data_map_offset;
    /*数据块信息写回*/
    newfs_super_d.data_blks =  newfs_super.data_blks;
	newfs_super_d.data_offset = newfs_super.data_offset;

    /*超级块写回磁盘*/
    if (newfs_driver_write(NEWFS_SUPER_OFS, (uint8_t *)&newfs_super_d, 
                     sizeof(struct newfs_super_d)) != NEWFS_ERROR_NONE) {
        return -NEWFS_ERROR_IO;
    }
    /*索引位图写回磁盘*/
    if (newfs_driver_write(newfs_super_d.ino_map_offset, (uint8_t *)(newfs_super.ino_map), 
                         NEWFS_BLKS_SZ(newfs_super_d.ino_map_blks)) != NEWFS_ERROR_NONE) {
        return -NEWFS_ERROR_IO;
    }

    /*数据位图写回磁盘*/
    if (newfs_driver_write(newfs_super_d.data_map_offset, (uint8_t *)(newfs_super.data_map), 
                         NEWFS_BLKS_SZ(newfs_super_d.data_map_blks)) != NEWFS_ERROR_NONE) {
        return -NEWFS_ERROR_IO;
    }

    free(newfs_super.ino_map);
    free(newfs_super.data_map);
    /*关闭驱动*/
    ddriver_close(NEWFS_DRIVER());
    printf("FINISH UNMOUNT!!!\n");

    return NEWFS_ERROR_NONE;
}

/**
 * @brief 计算路径的层级
 * exm: /av/c/d/f
 * -> lvl = 4
 * @param path 
 * @return int 
 */
int newfs_calc_lvl(const char * path) {
    // char* path_cpy = (char *)malloc(strlen(path));
    // strcpy(path_cpy, path);
    char* str = path;
    int   lvl = 0;
    if (strcmp(path, "/") == 0) {
        return lvl;
    }
    while (*str != NULL) {
        if (*str == '/') {
            lvl++;
        }
        str++;
    }
    return lvl;
}
/**
 * @brief 获取文件名
 * 
 * @param path 
 * @return char* 
 */
char* newfs_get_fname(const char* path) {
    char ch = '/';
    char *q = strrchr(path, ch) + 1;
    return q;
}
/**
 * @brief 查找文件或目录
 * path: /qwe/ad  total_lvl = 2,
 *      1) find /'s inode       lvl = 1
 *      2) find qwe's dentry 
 *      3) find qwe's inode     lvl = 2
 *      4) find ad's dentry
 *
 * path: /qwe     total_lvl = 1,
 *      1) find /'s inode       lvl = 1
 *      2) find qwe's dentry
 *  
 * 
 * 如果能查找到，返回该目录项
 * 如果查找不到，返回的是上一个有效的路径
 * 
 * path: /a/b/c
 *      1) find /'s inode     lvl = 1
 *      2) find a's dentry 
 *      3) find a's inode     lvl = 2
 *      4) find b's dentry    如果此时找不到了，is_find=FALSE且返回的是a的inode对应的dentry
 * 
 * @param path 
 * @return struct newfs_dentry* 
 */
struct newfs_dentry* newfs_lookup(const char * path, boolean* is_find, boolean* is_root){
    int total_lvl = newfs_calc_lvl(path);
    struct newfs_dentry* dentry_cursor = newfs_super.root_dentry;
    struct newfs_dentry* dentry_ret = NULL;
    struct newfs_inode*  inode; 
    int   lvl = 0;
    boolean is_hit;
    char* fname = NULL;
    char* path_cpy = (char*)malloc(sizeof(path));
    *is_root = FALSE;
    strcpy(path_cpy, path);

    if (total_lvl == 0) {                           /* 根目录 */
        *is_find = TRUE;
        *is_root = TRUE;
        dentry_ret = newfs_super.root_dentry;
    }
    printf("root_dentry ino: %d\n",dentry_cursor->inode->ino);
    printf("root_dentry fist child:%s\n", dentry_cursor->inode->dentrys->fname);

    fname = strtok(path_cpy, "/");       
    while (fname)
    {   
        lvl++;
        if (dentry_cursor->inode == NULL) {           /* Cache机制 */
            newfs_read_inode(dentry_cursor, dentry_cursor->ino);
        }

        inode = dentry_cursor->inode;

        if (NEWFS_IS_REG(inode) && lvl < total_lvl) { /*如果该文件为普通文件但却不在路径的末尾，则报错*/
            NEWFS_DBG("[%s] not a dir\n", __func__);
            dentry_ret = inode->dentry;
            break;
        }
        if (NEWFS_IS_DIR(inode)) {
            dentry_cursor = inode->dentrys;
            is_hit        = FALSE;

            while (dentry_cursor)   /* 遍历子目录项 */
            {
                if (memcmp(dentry_cursor->fname, fname, strlen(fname)) == 0) {
                    is_hit = TRUE;
                    break;
                }
                printf("%s\n", dentry_cursor->fname);
                dentry_cursor = dentry_cursor->brother;
            }
            
            if (!is_hit) {
                *is_find = FALSE;
                NEWFS_DBG("[%s] not found %s\n", __func__, fname);
                dentry_ret = inode->dentry;
                break;
            }

            if (is_hit && lvl == total_lvl) {
                *is_find = TRUE;
                dentry_ret = dentry_cursor;
                break;
            }
        }
        fname = strtok(NULL, "/"); 
    }

    if (dentry_ret->inode == NULL) {
        dentry_ret->inode = newfs_read_inode(dentry_ret, dentry_ret->ino);
    }
    
    return dentry_ret;

}


/**
 * @brief 
 * 
 * @param inode 
 * @param dir [0...]
 * @return struct newfs_dentry* 
 */
struct newfs_dentry*  newfs_get_dentry(struct newfs_inode * inode, int dir) {
    struct newfs_dentry* dentry_cursor = inode->dentrys;
    int    cnt = 0;
    while (dentry_cursor)
    {
        if (dir == cnt) {
            return dentry_cursor;
        }
        cnt++;
        dentry_cursor = dentry_cursor->brother;
    }
    return NULL;
}
/**
 * @brief 将dentry从inode的dentrys中取出
 * 
 * @param inode 一个目录的索引结点
 * @param dentry 该目录下的一个目录项
 * @return int 
 */
int newfs_drop_dentry(struct newfs_inode * inode, struct newfs_dentry * dentry) {
    boolean is_find = FALSE;
    struct newfs_dentry* dentry_cursor;
    dentry_cursor = inode->dentrys;
    
    if (dentry_cursor == dentry) {
        inode->dentrys = dentry->brother;
        is_find = TRUE;
    }
    else {
        while (dentry_cursor)
        {
            if (dentry_cursor->brother == dentry) {
                dentry_cursor->brother = dentry->brother;
                is_find = TRUE;
                break;
            }
            dentry_cursor = dentry_cursor->brother;
        }
    }
    if (!is_find) {
        return -NEWFS_ERROR_NOTFOUND;
    }
    inode->dir_cnt--;
    return inode->dir_cnt;
}


int newfs_drop_inode(struct newfs_inode * inode) {
    struct newfs_dentry*  dentry_cursor;
    struct newfs_dentry*  dentry_to_free;
    struct newfs_inode*   inode_cursor;

    int byte_cursor = 0; 
    int bit_cursor  = 0; 
    int ino_cursor  = 0;
    int data_cursor = 0;
    boolean is_find = FALSE;

    if (inode == newfs_super.root_dentry->inode) {
        return NEWFS_ERROR_INVAL;
    }

    if (NEWFS_IS_DIR(inode)) {
        dentry_cursor = inode->dentrys;
                                                      /* 递归向下drop */
        while (dentry_cursor)
        {   
            inode_cursor = dentry_cursor->inode;
            newfs_drop_inode(inode_cursor);
            newfs_drop_dentry(inode, dentry_cursor);
            dentry_to_free = dentry_cursor;
            dentry_cursor = dentry_cursor->brother;
            free(dentry_to_free);
        }

        for (byte_cursor = 0; byte_cursor < NEWFS_BLKS_SZ(newfs_super.ino_map_blks); 
            byte_cursor++)                            /* 调整inodemap */
        {
            for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++) {
                if (ino_cursor == inode->ino) {
                     newfs_super.ino_map[byte_cursor] &= (uint8_t)(~(0x1 << bit_cursor));
                     is_find = TRUE;
                     break;
                }
                ino_cursor++;
            }
            if (is_find == TRUE) {
                break;
            }
        }
    }
    else if (NEWFS_IS_REG(inode) || NEWFS_IS_SYM_LINK(inode)) {
        for (byte_cursor = 0; byte_cursor < NEWFS_BLKS_SZ(newfs_super.ino_map_blks); 
            byte_cursor++)                            /* 调整inodemap */
        {
            for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++) {
                if (ino_cursor == inode->ino) {
                     newfs_super.ino_map[byte_cursor] &= (uint8_t)(~(0x1 << bit_cursor));
                     is_find = TRUE;
                     break;
                }
                ino_cursor++;
            }
            if (is_find == TRUE) {
                break;
            }
        }
        //删除data
        if (inode->data)
            free(inode->data);
        //清空data位图
        int find_data_blks_num = 0;
        for (byte_cursor = 0; byte_cursor < NEWFS_BLKS_SZ(newfs_super.data_map_blks); //字节位
			byte_cursor++)
		{
			for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++) {          //比特位
				if(data_cursor == inode->blk_pointers[find_data_blks_num]) {    
														/* 当前data_cursor位置空闲 */
					newfs_super.data_map[byte_cursor] &= (uint8_t)(~(0x1 << bit_cursor));
					find_data_blks_num += 1;           
					break;
				}
				data_cursor++;
			}
			if (find_data_blks_num == inode->allocated_nums) {
				break;
			}
		}
        free(inode);
    }
    return NEWFS_ERROR_NONE;
}