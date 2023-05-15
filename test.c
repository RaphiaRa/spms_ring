#include "spms.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>

#define TEST(x)                                                        \
    if (x == 0)                                                        \
    {                                                                  \
        printf("Test failed: %s, at %s:%d\n", #x, __FILE__, __LINE__); \
        return -1;                                                     \
    }

int test_spms_pub_ctor_dtor()
{
    {
        spms_pub *pub;
        TEST(spms_pub_create(&pub, "spms_ring_unit_test", NULL, 0) == 0);
        spms_pub_free(pub);
    }
    {
        spms_pub *pub;
        struct spms_config config = {1024, 1024};
        TEST(spms_pub_create(&pub, "spms_ring_unit_test", &config, 0) == 0);
        spms_pub_free(pub);
    }
    return 0;
}

int test_spms_sub_ctor_dtor()
{
    {
        spms_sub *sub;
        TEST(spms_sub_create(&sub, "spms_ring_unit_test") != 0);
    }
    {
        spms_sub *sub;
        spms_pub *pub;
        TEST(spms_pub_create(&pub, "spms_ring_unit_test", NULL, 0) == 0);
        TEST(spms_sub_create(&sub, "spms_ring_unit_test") == 0);
        spms_sub_free(sub);
        spms_pub_free(pub);
    }
    return 0;
}

int main()
{
    TEST(test_spms_pub_ctor_dtor() == 0);
    TEST(test_spms_sub_ctor_dtor() == 0);
    printf("All tests passed\n");
    return 0;
}
