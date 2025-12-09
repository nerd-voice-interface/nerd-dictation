#include <gtest/gtest.h>
#include "nerd_dictation/input/postprocess.hpp"
#include <vector>
#include <string>

using namespace nerd_dictation::input;

class PostProcessTest : public ::testing::Test {
protected:
    void SetUp() override {
        processor_.reset();
    }
    
    TextPostProcessor processor_;
};

// Test punctuation replacement
TEST_F(PostProcessTest, PunctuationPeriod) {
    auto actions = processor_.process("привет точка мир");
    ASSERT_EQ(actions.size(), 1);
    EXPECT_TRUE(std::holds_alternative<TextAction>(actions[0]));
    auto text_action = std::get<TextAction>(actions[0]);
    EXPECT_EQ(text_action.text, "привет. мир");
}

TEST_F(PostProcessTest, PunctuationComma) {
    auto actions = processor_.process("текст запятая продолжение");
    ASSERT_EQ(actions.size(), 1);
    EXPECT_TRUE(std::holds_alternative<TextAction>(actions[0]));
    auto text_action = std::get<TextAction>(actions[0]);
    EXPECT_EQ(text_action.text, "текст, продолжение");
}

TEST_F(PostProcessTest, PunctuationColon) {
    auto actions = processor_.process("список двоеточие первый второй");
    ASSERT_EQ(actions.size(), 1);
    EXPECT_TRUE(std::holds_alternative<TextAction>(actions[0]));
    auto text_action = std::get<TextAction>(actions[0]);
    EXPECT_EQ(text_action.text, "список: первый второй");
}

TEST_F(PostProcessTest, PunctuationExclamation) {
    auto actions = processor_.process("восклицательный знак");
    ASSERT_EQ(actions.size(), 1);
    EXPECT_TRUE(std::holds_alternative<TextAction>(actions[0]));
    auto text_action = std::get<TextAction>(actions[0]);
    EXPECT_EQ(text_action.text, "!");
}

TEST_F(PostProcessTest, PunctuationQuestion) {
    auto actions = processor_.process("вопросительный знак");
    ASSERT_EQ(actions.size(), 1);
    EXPECT_TRUE(std::holds_alternative<TextAction>(actions[0]));
    auto text_action = std::get<TextAction>(actions[0]);
    EXPECT_EQ(text_action.text, "?");
}

TEST_F(PostProcessTest, PunctuationEllipsis) {
    auto actions = processor_.process("многоточие");
    ASSERT_EQ(actions.size(), 1);
    EXPECT_TRUE(std::holds_alternative<TextAction>(actions[0]));
    auto text_action = std::get<TextAction>(actions[0]);
    EXPECT_EQ(text_action.text, "...");
}

TEST_F(PostProcessTest, PunctuationQuotes) {
    auto actions = processor_.process("открыть кавычки текст закрыть кавычки");
    ASSERT_EQ(actions.size(), 1);
    EXPECT_TRUE(std::holds_alternative<TextAction>(actions[0]));
    auto text_action = std::get<TextAction>(actions[0]);
    EXPECT_EQ(text_action.text, "«текст»");
}

TEST_F(PostProcessTest, PunctuationParentheses) {
    auto actions = processor_.process("открыть скобку текст закрыть скобку");
    ASSERT_EQ(actions.size(), 1);
    EXPECT_TRUE(std::holds_alternative<TextAction>(actions[0]));
    auto text_action = std::get<TextAction>(actions[0]);
    EXPECT_EQ(text_action.text, "(текст)");
}

TEST_F(PostProcessTest, PunctuationNewline) {
    auto actions = processor_.process("новая строка");
    ASSERT_EQ(actions.size(), 1);
    EXPECT_TRUE(std::holds_alternative<TextAction>(actions[0]));
    auto text_action = std::get<TextAction>(actions[0]);
    EXPECT_EQ(text_action.text, "\n");
}

TEST_F(PostProcessTest, PunctuationParagraph) {
    auto actions = processor_.process("новый абзац");
    ASSERT_EQ(actions.size(), 1);
    EXPECT_TRUE(std::holds_alternative<TextAction>(actions[0]));
    auto text_action = std::get<TextAction>(actions[0]);
    EXPECT_EQ(text_action.text, "\n\n");
}

// Test key commands
TEST_F(PostProcessTest, KeyCommandExecute) {
    auto actions = processor_.process("выполнить");
    ASSERT_EQ(actions.size(), 1);
    EXPECT_TRUE(std::holds_alternative<KeyAction>(actions[0]));
    auto key_action = std::get<KeyAction>(actions[0]);
    EXPECT_EQ(key_action.keys, "ctrl+Return");
}

TEST_F(PostProcessTest, KeyCommandVariants) {
    std::vector<std::string> variants = {"выполнять", "выполняй", "выполни"};
    for (const auto& variant : variants) {
        auto actions = processor_.process(variant);
        ASSERT_EQ(actions.size(), 1) << "Failed for variant: " << variant;
        EXPECT_TRUE(std::holds_alternative<KeyAction>(actions[0]));
        auto key_action = std::get<KeyAction>(actions[0]);
        EXPECT_EQ(key_action.keys, "ctrl+Return");
    }
}

// Test emoji commands
TEST_F(PostProcessTest, EmojiSmile) {
    auto actions = processor_.process("смайлик улыбка");
    ASSERT_EQ(actions.size(), 1);
    EXPECT_TRUE(std::holds_alternative<TextAction>(actions[0]));
    auto text_action = std::get<TextAction>(actions[0]);
    EXPECT_EQ(text_action.text, "😊");
}

TEST_F(PostProcessTest, EmojiLaugh) {
    auto actions = processor_.process("смайлик смех");
    ASSERT_EQ(actions.size(), 1);
    EXPECT_TRUE(std::holds_alternative<TextAction>(actions[0]));
    auto text_action = std::get<TextAction>(actions[0]);
    EXPECT_EQ(text_action.text, "😂");
}

TEST_F(PostProcessTest, EmojiHeart) {
    auto actions = processor_.process("смайлик сердце");
    ASSERT_EQ(actions.size(), 1);
    EXPECT_TRUE(std::holds_alternative<TextAction>(actions[0]));
    auto text_action = std::get<TextAction>(actions[0]);
    EXPECT_EQ(text_action.text, "❤️");
}

// Test capitalization
TEST_F(PostProcessTest, CapitalizationAfterPeriod) {
    processor_.reset();
    auto actions = processor_.process("привет точка мир");
    ASSERT_EQ(actions.size(), 1);
    EXPECT_TRUE(std::holds_alternative<TextAction>(actions[0]));
    auto text_action = std::get<TextAction>(actions[0]);
    EXPECT_EQ(text_action.text, "привет. Мир");
}

TEST_F(PostProcessTest, CapitalizationAfterExclamation) {
    processor_.reset();
    auto actions = processor_.process("текст восклицательный знак новый текст");
    ASSERT_EQ(actions.size(), 1);
    EXPECT_TRUE(std::holds_alternative<TextAction>(actions[0]));
    auto text_action = std::get<TextAction>(actions[0]);
    EXPECT_EQ(text_action.text, "текст! Новый текст");
}

TEST_F(PostProcessTest, CapitalizationAfterQuestion) {
    processor_.reset();
    auto actions = processor_.process("вопрос вопросительный знак ответ");
    ASSERT_EQ(actions.size(), 1);
    EXPECT_TRUE(std::holds_alternative<TextAction>(actions[0]));
    auto text_action = std::get<TextAction>(actions[0]);
    EXPECT_EQ(text_action.text, "вопрос? Ответ");
}

TEST_F(PostProcessTest, CapitalizationAfterNewline) {
    processor_.reset();
    auto actions = processor_.process("первая строка новая строка вторая строка");
    ASSERT_EQ(actions.size(), 1);
    EXPECT_TRUE(std::holds_alternative<TextAction>(actions[0]));
    auto text_action = std::get<TextAction>(actions[0]);
    EXPECT_EQ(text_action.text, "первая строка\nВторая строка");
}

TEST_F(PostProcessTest, CapitalizationAfterEmoji) {
    processor_.reset();
    auto actions = processor_.process("привет смайлик улыбка мир");
    ASSERT_EQ(actions.size(), 1);
    EXPECT_TRUE(std::holds_alternative<TextAction>(actions[0]));
    auto text_action = std::get<TextAction>(actions[0]);
    EXPECT_EQ(text_action.text, "привет😊 Мир");
}

// Test complex phrases
TEST_F(PostProcessTest, ComplexMixedPunctuation) {
    auto actions = processor_.process("привет запятая как дела точка");
    ASSERT_EQ(actions.size(), 1);
    EXPECT_TRUE(std::holds_alternative<TextAction>(actions[0]));
    auto text_action = std::get<TextAction>(actions[0]);
    EXPECT_EQ(text_action.text, "привет, как дела.");
}

TEST_F(PostProcessTest, ComplexWithKeyCommand) {
    auto actions = processor_.process("начало выполнить конец");
    ASSERT_GE(actions.size(), 2); // At least text + key action
    
    // Check that key action is present
    bool has_key_action = false;
    for (const auto& action : actions) {
        if (std::holds_alternative<KeyAction>(action)) {
            has_key_action = true;
            auto key_action = std::get<KeyAction>(action);
            EXPECT_EQ(key_action.keys, "ctrl+Return");
        }
    }
    EXPECT_TRUE(has_key_action);
}

TEST_F(PostProcessTest, ComplexMultiplePunctuation) {
    auto actions = processor_.process("список двоеточие первый запятая второй точка");
    ASSERT_EQ(actions.size(), 1);
    EXPECT_TRUE(std::holds_alternative<TextAction>(actions[0]));
    auto text_action = std::get<TextAction>(actions[0]);
    EXPECT_EQ(text_action.text, "список: первый, второй.");
}

TEST_F(PostProcessTest, ComplexLong) {
    processor_.reset();
    auto actions = processor_.process("привет точка это тест запятая проверка работы системы точка");
    ASSERT_EQ(actions.size(), 1);
    EXPECT_TRUE(std::holds_alternative<TextAction>(actions[0]));
    auto text_action = std::get<TextAction>(actions[0]);
    EXPECT_EQ(text_action.text, "Привет. Это тест, проверка работы системы.");
}

// Test empty input
TEST_F(PostProcessTest, EmptyInput) {
    auto actions = processor_.process("");
    EXPECT_TRUE(actions.empty());
}

// Test reset
TEST_F(PostProcessTest, Reset) {
    processor_.process("привет точка");
    processor_.reset();
    
    // After reset, capitalization should start fresh
    auto actions = processor_.process("мир");
    ASSERT_EQ(actions.size(), 1);
    EXPECT_TRUE(std::holds_alternative<TextAction>(actions[0]));
    auto text_action = std::get<TextAction>(actions[0]);
    EXPECT_EQ(text_action.text, "Мир"); // Capitalized because it's the start
}
