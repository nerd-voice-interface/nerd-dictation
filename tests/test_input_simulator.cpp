#include <gtest/gtest.h>
#include "nerd_dictation/input/input_simulator.hpp"
#include <sstream>
#include <iostream>

using namespace nerd_dictation::input;

class InputSimulatorTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Redirect stdout for testing
        old_cout = std::cout.rdbuf();
        cout_buffer.str("");
        std::cout.rdbuf(cout_buffer.rdbuf());
    }
    
    void TearDown() override {
        std::cout.rdbuf(old_cout);
    }
    
    std::streambuf* old_cout;
    std::stringstream cout_buffer;
};

TEST_F(InputSimulatorTest, StdoutMethod) {
    InputSimulator sim("stdout");
    
    sim.type_text("test text");
    
    std::string output = cout_buffer.str();
    EXPECT_EQ(output, "test text");
}

TEST_F(InputSimulatorTest, StdoutKeyPress) {
    InputSimulator sim("stdout");
    
    sim.press_keys("ctrl+Return");
    
    std::string output = cout_buffer.str();
    EXPECT_EQ(output, "[KEY:ctrl+Return]");
}

TEST_F(InputSimulatorTest, InvalidMethodFallsBackToStdout) {
    InputSimulator sim("invalid_method");
    
    // Should fall back to stdout
    sim.type_text("test");
    
    std::string output = cout_buffer.str();
    EXPECT_EQ(output, "test");
}

TEST_F(InputSimulatorTest, EmptyText) {
    InputSimulator sim("stdout");
    
    sim.type_text("");
    
    std::string output = cout_buffer.str();
    EXPECT_TRUE(output.empty());
}

TEST_F(InputSimulatorTest, UnicodeText) {
    InputSimulator sim("stdout");
    
    sim.type_text("привет мир");
    
    std::string output = cout_buffer.str();
    EXPECT_EQ(output, "привет мир");
}

TEST_F(InputSimulatorTest, SpecialCharacters) {
    InputSimulator sim("stdout");
    
    sim.type_text("test@example.com");
    
    std::string output = cout_buffer.str();
    EXPECT_EQ(output, "test@example.com");
}
