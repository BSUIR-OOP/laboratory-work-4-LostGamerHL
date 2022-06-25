#include <gtest/gtest.h>
#include "depinject.h"

using depInject::Container;
using depInject::InjectionContext;
using depInject::ComponentNotFoundException;
using depInject::CircularDependencyFound;

namespace SimpleResolve
{
    class IRunner
    {
    public:
        virtual ~IRunner() {}
    };

    class Cheetah : public IRunner
    {
    public:
        INJECT(Cheetah()) {}
    };

    TEST(depInjectTest, TestSimpleResolve)
    {
        Container c;
        c.bind<IRunner>().to<Cheetah>();

        std::shared_ptr<IRunner> runner = c.get<IRunner>();
        std::shared_ptr<IRunner> runner2 = c.get<IRunner>();

        EXPECT_EQ(1, runner.use_count());
        EXPECT_EQ(1, runner2.use_count());
        EXPECT_NE(nullptr, dynamic_cast<Cheetah*>(runner.get()));
        EXPECT_NE(nullptr, dynamic_cast<Cheetah*>(runner2.get()));
        EXPECT_NE(runner, runner2);
    }

    TEST(depInjectTest, TestSimpleResolve__Singleton)
    {
        Container c;
        c.bind<IRunner>().to<Cheetah>().InSingletonScope();

        std::shared_ptr<IRunner> runner = c.get<IRunner>();
        std::shared_ptr<IRunner> runner2 = c.get<IRunner>();

        EXPECT_EQ(3, runner.use_count());
        EXPECT_EQ(3, runner2.use_count());
        EXPECT_NE(nullptr, dynamic_cast<Cheetah*>(runner.get()));
        EXPECT_NE(nullptr, dynamic_cast<Cheetah*>(runner2.get()));
        EXPECT_EQ(runner, runner2);
    }

    TEST(depInjectTest, TestSimpleResolve_ToSelf)
    {
        Container c;
        c.bind<Cheetah>().to<Cheetah>();

        std::shared_ptr<Cheetah> runner = c.get<Cheetah>();
        std::shared_ptr<Cheetah> runner2 = c.get<Cheetah>();

        EXPECT_EQ(1, runner.use_count());
        EXPECT_EQ(1, runner2.use_count());
        EXPECT_NE(nullptr, runner);
        EXPECT_NE(nullptr, runner2);
        EXPECT_NE(runner, runner2);
    }

    TEST(depInjectTest, TestSimpleResolve_ToSelf__Singleton)
    {
        Container c;
        c.bind<Cheetah>().to<Cheetah>().InSingletonScope();

        std::shared_ptr<Cheetah> runner = c.get<Cheetah>();
        std::shared_ptr<Cheetah> runner2 = c.get<Cheetah>();

        EXPECT_EQ(3, runner.use_count());
        EXPECT_EQ(3, runner2.use_count());
        EXPECT_NE(nullptr, runner);
        EXPECT_NE(nullptr, runner2);
        EXPECT_EQ(runner, runner2);
    }
}

namespace NestedDependencies
{
    class INest
    {
    public:
        virtual ~INest() {}
    };

    class SpiderNest : public INest
    {
    public:
        INJECT(SpiderNest()) {}
    };

    class Spider
    {
    public:
        INJECT(Spider(std::shared_ptr<INest> nest)) :
            nest(nest)
        {}

        std::shared_ptr<INest> nest;
    };

    TEST(depInjectTest, TestNestedDependencies)
    {
        Container c;
        c.bind<Spider>().ToSelf();
        c.bind<INest>().to<SpiderNest>().InSingletonScope();

        std::shared_ptr<Spider> spider1 = c.get<Spider>();
        std::shared_ptr<Spider> spider2 = c.get<Spider>();
        std::shared_ptr<Spider> spider3 = c.get<Spider>();
        std::shared_ptr<INest> nest = c.get<INest>();

        EXPECT_NE(spider2.get(), spider1.get());
        EXPECT_NE(spider3.get(), spider1.get());
        EXPECT_NE(spider3.get(), spider2.get());

        EXPECT_EQ(1, spider1.use_count());
        EXPECT_EQ(1, spider2.use_count());
        EXPECT_EQ(1, spider3.use_count());
        EXPECT_EQ(5, nest.use_count());
        EXPECT_NE(nullptr, dynamic_cast<SpiderNest*>(nest.get()));
    }
}

namespace ComponentNotFound
{
    class IRunner
    {
    public:
        virtual ~IRunner() {}
    };

    TEST(depInjectTest, TestComponentNotFound)
    {
        Container c;

        ASSERT_THROW(c.get<IRunner>(), ComponentNotFoundException);
    }
}

namespace CircularDependency
{
    class Middle;
    class End;

    class Start
    {
    public:
        INJECT(Start(std::shared_ptr<Middle> middle)) {}
    };

    class Middle
    {
    public:
        INJECT(Middle(std::shared_ptr<End> end)) {}
    };

    class End
    {
    public:
        INJECT(End(std::shared_ptr<Start> start)) {}
    };

    TEST(depInjectTest, TestCircularDependency)
    {
        Container c;

        // intentional order to not match the function implementation order
        c.bind<Start>().ToSelf();
        c.bind<Middle>().ToSelf();
        c.bind<End>().ToSelf();

        ASSERT_THROW(c.get<Start>(), CircularDependencyFound);
    }
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
