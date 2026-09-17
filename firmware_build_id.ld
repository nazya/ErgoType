SECTIONS
{
    .note.gnu.build-id :
    {
        __firmware_build_id_start = . + 16;
        KEEP(*(.note.gnu.build-id))
        __firmware_build_id_end = .;
    }
}
INSERT AFTER .rodata;

ASSERT(__firmware_build_id_end - __firmware_build_id_start == 20,
       "unexpected firmware build ID size")
