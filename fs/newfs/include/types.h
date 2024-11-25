#ifndef _TYPES_H_
#define _TYPES_H_

typedef int  boolean;
struct custom_options {
	const char*        device;
};

typedef enum newfs_file_type {
    NEWFS_REG_FILE,
    NEWFS_DIR,
    NEWFS_SYM_LINK
} NEWFS_FILE_TYPE;


#define MAX_NAME_LEN            128 

#define TRUE                    1
#define FALSE                   0

/******************************************************************************
* SECTION: Macro
*******************************************************************************/
#define TRUE                    1
#define FALSE                   0
#define UINT32_BITS             32
#define UINT8_BITS              8

#define NEWFS_SUPER_OFS           0
#define NEWFS_ROOT_INO            0



#define NEWFS_ERROR_NONE          0
#define NEWFS_ERROR_ACCESS        EACCES
#define NEWFS_ERROR_SEEK          ESPIPE     
#define NEWFS_ERROR_ISDIR         EISDIR
#define NEWFS_ERROR_NOSPACE       ENOSPC
#define NEWFS_ERROR_EXISTS        EEXIST
#define NEWFS_ERROR_NOTFOUND      ENOENT
#define NEWFS_ERROR_UNSUPPORTED   ENXIO
#define NEWFS_ERROR_IO            EIO     /* Error Input/Output */
#define NEWFS_ERROR_INVAL         EINVAL  /* Invalid Args */

#define NEWFS_MAX_FILE_NAME       128
#define NEWFS_INODE_PER_FILE      1
#define NEWFS_DATA_PER_FILE       6
#define NEWFS_DEFAULT_PERM        0777

#define NEWFS_IOC_MAGIC           'S'
#define NEWFS_IOC_SEEK            _IO(NEWFS_IOC_MAGIC, 0)

#define NEWFS_FLAG_BUF_DIRTY      0x1
#define NEWFS_FLAG_BUF_OCCUPY     0x2
/******************************************************************************
* SECTION: Macro Function
*******************************************************************************/
#define NEWFS_IO_SZ()                     (newfs_super.io_size)
#define NEWFS_DISK_SZ()                   (newfs_super.disk_size)
#define NEWFS_DRIVER()                    (newfs_super.driver_fd)
#define NEWFS_BLK_SZ()                    (newfs_super.blk_size)
#define NEWFS_BLKS_SZ(blks)               ((blks) * NEWFS_BLK_SZ())


#define NEWFS_ROUND_DOWN(value, round)    ((value) % (round) == 0 ? (value) : ((value) / (round)) * (round))
#define NEWFS_ROUND_UP(value, round)      ((value) % (round) == 0 ? (value) : ((value) / (round) + 1) * (round))



#define NEWFS_ASSIGN_FNAME(psfs_dentry, _fname) \
(memcpy(psfs_dentry->fname, _fname, strlen(_fname)))

#define NEWFS_INODES_PER_BLK                16
#define NEWFS_INO_OFS(ino) \
    (newfs_super.data_map_offset + ((ino) / NEWFS_INODES_PER_BLK) * NEWFS_BLK_SZ() + ((ino) % NEWFS_INODES_PER_BLK) * sizeof(struct newfs_inode_d))
#define NEWFS_DATA_OFS(ino)               (newfs_super.data_offset + NEWFS_BLKS_SZ(ino))

#define NEWFS_IS_DIR(pinode)              (pinode->dentry->ftype == NEWFS_DIR)
#define NEWFS_IS_REG(pinode)              (pinode->dentry->ftype == NEWFS_REG_FILE)
#define NEWFS_IS_SYM_LINK(pinode)         (pinode->dentry->ftype == NEWFS_SYM_LINK)

//超级块
struct newfs_super {
    uint32_t magic_num;             //幻数
    int driver_fd;              //设备文件描述符

    boolean is_mounted;         //是否被挂载

    int blk_size;               //逻辑块大小
    int io_size;                //IO大小
    int disk_size;              //磁盘大小
    int usage_size;             //已使用大小

    int sb_offset;              // 超级块于磁盘中的偏移，通常默认为0
    int sb_blks;                // 超级块于磁盘中的块数，通常默认为1

    int ino_map_offset;         //索引节点位图的偏移
    int ino_map_blks;           //索引节点位图占用逻辑块数量
    uint8_t* ino_map;

    int data_map_offset;        //数据块位图偏移
    int data_map_blks;          //数据块位图占用逻辑块数量
    uint8_t* data_map;

    int ino_offset;             //索引节点的偏移
    int ino_blks;               //索引节点占用逻辑块数量

    int data_offset;            //数据块偏移
    int data_blks;             //数据块占用逻辑块数量

    struct newfs_dentry* root_dentry; //根目录索引
    int ino_max;                // 最大支持inode数
    int blks_nums;              //逻辑块块数
};

struct newfs_super_d{

    uint32_t magic_num;             //幻数

    int blks_size;              // 逻辑块大小
    int io_size;                //IO大小
    int disk_size;              //磁盘大小
    int usage_size;             //已使用大小

    int sb_offset;              // 超级块于磁盘中的偏移，通常默认为0
    int sb_blks;                // 超级块于磁盘中的块数，通常默认为1

    int ino_map_offset;         //索引节点位图的偏移
    int ino_map_blks;           //索引节点位图占用逻辑块数量

    int data_map_offset;        //数据块位图偏移
    int data_map_blks;          //数据块位图占用逻辑块数量

    int ino_offset;             //索引节点的偏移
    int ino_blks;               //索引节点占用逻辑块数量

    int data_offset;            //数据块偏移
    int data_blks;              //数据块占用逻辑块数量

    int ino_max;                // 最大支持inode数
    int blks_nums;              //逻辑块块数

};

struct newfs_inode {
    uint32_t           ino;                           // 在inode位图中的下标
    int                size;                          /* 文件已占用空间 */
    int                blk_pointers[NEWFS_DATA_PER_FILE]; /*数据块地址指针*/
    uint8_t*           data[NEWFS_DATA_PER_FILE];     /* 数据块内容指针（可固定分配）*/
    int                link;                          /* 链接数，默认为1 */
    struct newfs_dentry* dentry;                        /* 指向该inode的dentry */
    struct newfs_dentry* dentrys;                       /* 所有目录项 */
    char               target_path[NEWFS_MAX_FILE_NAME];/* store traget path when it is a symlink */
    NEWFS_FILE_TYPE    ftype;
    int                dir_cnt;                      // 如果是目录类型文件，下面有几个目录项
};

struct newfs_inode_d {
    uint32_t           ino;                           // 在inode位图中的下标
    int                size;                          /* 文件已占用空间 */
    int                blk_pointers[NEWFS_DATA_PER_FILE];
    uint8_t*           data[NEWFS_DATA_PER_FILE];    /* 数据块指针（可固定分配）*/
    int                link;                          /* 链接数，默认为1 */
    char               target_path[NEWFS_MAX_FILE_NAME];/* store traget path when it is a symlink */
    NEWFS_FILE_TYPE    ftype;
    int                dir_cnt;                      // 如果是目录类型文件，下面有几个目录项
};


struct newfs_dentry {
    char                fname[MAX_NAME_LEN];        /*该文件的名字*/
    uint32_t            ino;                       /*该目录项指向的ino节点*/
    NEWFS_FILE_TYPE     ftype;                     /*该目录文件或者普通文件*/
    struct newfs_dentry* parent;                   /* 父亲Inode的dentry */
    struct newfs_dentry* brother; 
    struct newfs_inode*  inode;    
};

struct newfs_dentry_d {
    char                fname[MAX_NAME_LEN];        /*该文件的名字*/
    uint32_t            ino;                       /*该目录项指向的ino节点*/
    NEWFS_FILE_TYPE     ftype;                     /*该目录文件或者普通文件*/
};

#endif /* _TYPES_H_ */