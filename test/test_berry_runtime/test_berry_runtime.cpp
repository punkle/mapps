#include <mapps/BerryRuntime.h>
#include <unity.h>

#include <string>
#include <vector>

static void test_vm_lifecycle()
{
    BerryRuntime rt("test_app", {});
    TEST_ASSERT_FALSE(rt.isRunning());

    rt.setSource("var x = 1");
    TEST_ASSERT_TRUE(rt.start());
    TEST_ASSERT_TRUE(rt.isRunning());

    rt.stop();
    TEST_ASSERT_FALSE(rt.isRunning());
}

static void test_set_source()
{
    BerryRuntime rt("test_app", {});
    rt.setSource("var result = 42");
    TEST_ASSERT_TRUE(rt.start());
    TEST_ASSERT_TRUE(rt.isRunning());
    rt.stop();
}

static void test_call_function()
{
    BerryRuntime rt("test_app", {});
    rt.setSource("def add(a, b) return a + b end");
    TEST_ASSERT_TRUE(rt.start());

    std::vector<AppValue> args = {AppValue(3), AppValue(4)};
    AppValue result = rt.call("add", args);
    TEST_ASSERT_EQUAL(AppValue::Int, result.type);
    TEST_ASSERT_EQUAL(7, result.intVal);

    rt.stop();
}

static void test_call_no_args()
{
    BerryRuntime rt("test_app", {});
    rt.setSource("def hello() return 'world' end");
    TEST_ASSERT_TRUE(rt.start());

    AppValue result = rt.call("hello");
    TEST_ASSERT_EQUAL(AppValue::String, result.type);
    TEST_ASSERT_EQUAL_STRING("world", result.strVal.c_str());

    rt.stop();
}

static void test_call_nonexistent_function()
{
    BerryRuntime rt("test_app", {});
    rt.setSource("var x = 1");
    TEST_ASSERT_TRUE(rt.start());

    AppValue result = rt.call("nonexistent");
    TEST_ASSERT_EQUAL(AppValue::Nil, result.type);

    rt.stop();
}

static bool nativeWasCalled = false;

static void test_add_bindings()
{
    nativeWasCalled = false;
    BerryRuntime rt("test_app", {});

    std::map<std::string, NativeAppFunction> bindings;
    bindings["ping"] = [](const std::vector<AppValue> &) -> AppValue {
        nativeWasCalled = true;
        return AppValue(42);
    };
    rt.addBindings("test_mod", bindings);

    rt.setSource("var r = test_mod.ping()");
    TEST_ASSERT_TRUE(rt.start());
    TEST_ASSERT_TRUE(nativeWasCalled);

    rt.stop();
}

static void test_add_bindings_with_args()
{
    BerryRuntime rt("test_app", {});

    std::map<std::string, NativeAppFunction> bindings;
    bindings["multiply"] = [](const std::vector<AppValue> &args) -> AppValue {
        if (args.size() >= 2 && args[0].type == AppValue::Int && args[1].type == AppValue::Int)
            return AppValue(args[0].intVal * args[1].intVal);
        return AppValue();
    };
    rt.addBindings("math_ext", bindings);

    rt.setSource("def compute() return math_ext.multiply(6, 7) end");
    TEST_ASSERT_TRUE(rt.start());

    AppValue result = rt.call("compute");
    TEST_ASSERT_EQUAL(AppValue::Int, result.type);
    TEST_ASSERT_EQUAL(42, result.intVal);

    rt.stop();
}

static void test_set_bootstrap()
{
    BerryRuntime rt("test_app", {});

    std::map<std::string, NativeAppFunction> bindings;
    bindings["value"] = [](const std::vector<AppValue> &) -> AppValue {
        return AppValue(99);
    };
    rt.addBindings("custom", bindings);

    // Custom bootstrap wraps the native function differently
    rt.setBootstrap("custom", "class custom\n  static def value() return _custom_value() end\n  static def doubled() return _custom_value() * 2 end\nend\n");

    rt.setSource("def get_doubled() return custom.doubled() end");
    TEST_ASSERT_TRUE(rt.start());

    AppValue result = rt.call("get_doubled");
    TEST_ASSERT_EQUAL(AppValue::Int, result.type);
    TEST_ASSERT_EQUAL(198, result.intVal);

    rt.stop();
}

static void test_has_permission()
{
    BerryRuntime rt("test_app", {"http-client", "bluetooth"});
    TEST_ASSERT_TRUE(rt.hasPermission("http-client"));
    TEST_ASSERT_TRUE(rt.hasPermission("bluetooth"));
    TEST_ASSERT_FALSE(rt.hasPermission("gps"));
    TEST_ASSERT_FALSE(rt.hasPermission(""));
}

static void test_no_source()
{
    BerryRuntime rt("test_app", {});
    // Don't call setSource — start should fail
    TEST_ASSERT_FALSE(rt.start());
    TEST_ASSERT_FALSE(rt.isRunning());
}

static void test_return_types()
{
    BerryRuntime rt("test_app", {});
    rt.setSource(
        "def ret_int() return 42 end\n"
        "def ret_float() return 3.14 end\n"
        "def ret_string() return 'hello' end\n"
        "def ret_bool() return true end\n"
        "def ret_nil() return nil end\n");
    TEST_ASSERT_TRUE(rt.start());

    AppValue r1 = rt.call("ret_int");
    TEST_ASSERT_EQUAL(AppValue::Int, r1.type);
    TEST_ASSERT_EQUAL(42, r1.intVal);

    AppValue r2 = rt.call("ret_float");
    TEST_ASSERT_EQUAL(AppValue::Float, r2.type);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 3.14f, r2.floatVal);

    AppValue r3 = rt.call("ret_string");
    TEST_ASSERT_EQUAL(AppValue::String, r3.type);
    TEST_ASSERT_EQUAL_STRING("hello", r3.strVal.c_str());

    AppValue r4 = rt.call("ret_bool");
    TEST_ASSERT_EQUAL(AppValue::Bool, r4.type);
    TEST_ASSERT_TRUE(r4.boolVal);

    AppValue r5 = rt.call("ret_nil");
    TEST_ASSERT_EQUAL(AppValue::Nil, r5.type);

    rt.stop();
}

static bool cleanupWasCalled = false;

static void test_cleanup_called_on_stop()
{
    cleanupWasCalled = false;
    BerryRuntime rt("test_app", {});

    std::map<std::string, NativeAppFunction> bindings;
    bindings["set_flag"] = [](const std::vector<AppValue> &) -> AppValue {
        cleanupWasCalled = true;
        return AppValue();
    };
    rt.addBindings("test_mod", bindings);

    rt.setSource("def cleanup() test_mod.set_flag() end");
    TEST_ASSERT_TRUE(rt.start());
    TEST_ASSERT_FALSE(cleanupWasCalled);

    rt.stop();
    TEST_ASSERT_TRUE(cleanupWasCalled);
}

static void test_multiple_start_stop()
{
    BerryRuntime rt("test_app", {});
    rt.setSource("var x = 1");

    TEST_ASSERT_TRUE(rt.start());
    TEST_ASSERT_TRUE(rt.isRunning());
    rt.stop();
    TEST_ASSERT_FALSE(rt.isRunning());

    TEST_ASSERT_TRUE(rt.start());
    TEST_ASSERT_TRUE(rt.isRunning());
    rt.stop();
    TEST_ASSERT_FALSE(rt.isRunning());
}

int main()
{
    UNITY_BEGIN();

    RUN_TEST(test_vm_lifecycle);
    RUN_TEST(test_set_source);
    RUN_TEST(test_call_function);
    RUN_TEST(test_call_no_args);
    RUN_TEST(test_call_nonexistent_function);
    RUN_TEST(test_add_bindings);
    RUN_TEST(test_add_bindings_with_args);
    RUN_TEST(test_set_bootstrap);
    RUN_TEST(test_has_permission);
    RUN_TEST(test_no_source);
    RUN_TEST(test_return_types);
    RUN_TEST(test_cleanup_called_on_stop);
    RUN_TEST(test_multiple_start_stop);

    return UNITY_END();
}
