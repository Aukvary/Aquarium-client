#include <gtest/gtest.h>

#include <UtilsService.grpc.pb.h>

TEST(GrpcMockTest, ProtoLinkingSmokeTest) {
    aquarium::api::HealthRequest request;
    EXPECT_EQ(request.ByteSizeLong(), 0);
}
