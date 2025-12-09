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
    // After reset(), capitalize_next_ is true, so first word is capitalized
    EXPECT_EQ(text_action.text, "Привет. Мир");
}

TEST_F(PostProcessTest, PunctuationComma) {
    auto actions = processor_.process("текст запятая продолжение");
    ASSERT_EQ(actions.size(), 1);
    EXPECT_TRUE(std::holds_alternative<TextAction>(actions[0]));
    auto text_action = std::get<TextAction>(actions[0]);
    // After reset(), capitalize_next_ is true, so first word is capitalized
    EXPECT_EQ(text_action.text, "Текст, продолжение");
}

TEST_F(PostProcessTest, PunctuationColon) {
    auto actions = processor_.process("список двоеточие первый второй");
    ASSERT_EQ(actions.size(), 1);
    EXPECT_TRUE(std::holds_alternative<TextAction>(actions[0]));
    auto text_action = std::get<TextAction>(actions[0]);
    // After reset(), capitalize_next_ is true, so first word is capitalized
    EXPECT_EQ(text_action.text, "Список: первый второй");
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
    // After reset(), capitalize_next_ is true, so first word is capitalized
    EXPECT_EQ(text_action.text, "«Текст»");
}

TEST_F(PostProcessTest, PunctuationParentheses) {
    auto actions = processor_.process("открыть скобку текст закрыть скобку");
    ASSERT_EQ(actions.size(), 1);
    EXPECT_TRUE(std::holds_alternative<TextAction>(actions[0]));
    auto text_action = std::get<TextAction>(actions[0]);
    // After reset(), capitalize_next_ is true, so first word is capitalized
    EXPECT_EQ(text_action.text, "(Текст)");
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
    // After reset(), first word is capitalized, and after period second word is also capitalized
    EXPECT_EQ(text_action.text, "Привет. Мир");
}

TEST_F(PostProcessTest, CapitalizationAfterExclamation) {
    processor_.reset();
    auto actions = processor_.process("текст восклицательный знак новый текст");
    ASSERT_EQ(actions.size(), 1);
    EXPECT_TRUE(std::holds_alternative<TextAction>(actions[0]));
    auto text_action = std::get<TextAction>(actions[0]);
    // After reset(), first word is capitalized, and after ! second word is also capitalized
    EXPECT_EQ(text_action.text, "Текст! Новый текст");
}

TEST_F(PostProcessTest, CapitalizationAfterQuestion) {
    processor_.reset();
    auto actions = processor_.process("вопрос вопросительный знак ответ");
    ASSERT_EQ(actions.size(), 1);
    EXPECT_TRUE(std::holds_alternative<TextAction>(actions[0]));
    auto text_action = std::get<TextAction>(actions[0]);
    // After reset(), first word is capitalized, and after ? second word is also capitalized
    EXPECT_EQ(text_action.text, "Вопрос? Ответ");
}

TEST_F(PostProcessTest, CapitalizationAfterNewline) {
    processor_.reset();
    auto actions = processor_.process("первая строка новая строка вторая строка");
    ASSERT_EQ(actions.size(), 1);
    EXPECT_TRUE(std::holds_alternative<TextAction>(actions[0]));
    auto text_action = std::get<TextAction>(actions[0]);
    // After reset(), first word is capitalized, and after \n second word is also capitalized
    // Note: spaces around \n are preserved from the original text
    EXPECT_EQ(text_action.text, "Первая строка \n Вторая строка");
}

TEST_F(PostProcessTest, CapitalizationAfterEmoji) {
    processor_.reset();
    auto actions = processor_.process("привет смайлик улыбка мир");
    ASSERT_EQ(actions.size(), 1);
    EXPECT_TRUE(std::holds_alternative<TextAction>(actions[0]));
    auto text_action = std::get<TextAction>(actions[0]);
    // After reset(), first word is capitalized, and after emoji second word is also capitalized
    // Note: there's a space before emoji because "привет" and "смайлик улыбка" are separate words
    EXPECT_EQ(text_action.text, "Привет 😊 Мир");
}

// Test complex phrases
TEST_F(PostProcessTest, ComplexMixedPunctuation) {
    auto actions = processor_.process("привет запятая как дела точка");
    ASSERT_EQ(actions.size(), 1);
    EXPECT_TRUE(std::holds_alternative<TextAction>(actions[0]));
    auto text_action = std::get<TextAction>(actions[0]);
    // After reset(), capitalize_next_ is true, so first word is capitalized
    EXPECT_EQ(text_action.text, "Привет, как дела.");
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
    // After reset(), capitalize_next_ is true, so first word is capitalized
    EXPECT_EQ(text_action.text, "Список: первый, второй.");
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

// Test multiple spaces suppression
TEST_F(PostProcessTest, MultipleSpacesSuppression) {
    // Simulate recognition with multiple spaces
    auto actions = processor_.process("привет    мир");
    ASSERT_EQ(actions.size(), 1);
    EXPECT_TRUE(std::holds_alternative<TextAction>(actions[0]));
    auto text_action = std::get<TextAction>(actions[0]);
    // Multiple spaces should be normalized to single space
    EXPECT_EQ(text_action.text, "Привет мир");
}

TEST_F(PostProcessTest, MultipleSpacesWithPunctuation) {
    auto actions = processor_.process("привет    точка    мир");
    ASSERT_EQ(actions.size(), 1);
    EXPECT_TRUE(std::holds_alternative<TextAction>(actions[0]));
    auto text_action = std::get<TextAction>(actions[0]);
    // Multiple spaces should be normalized, but space before punctuation should be removed
    EXPECT_EQ(text_action.text, "Привет. Мир");
}

TEST_F(PostProcessTest, MultipleSpacesAtStart) {
    auto actions = processor_.process("   привет мир");
    ASSERT_EQ(actions.size(), 1);
    EXPECT_TRUE(std::holds_alternative<TextAction>(actions[0]));
    auto text_action = std::get<TextAction>(actions[0]);
    // Leading spaces should be removed
    EXPECT_EQ(text_action.text, "Привет мир");
}

TEST_F(PostProcessTest, MultipleSpacesAtEnd) {
    auto actions = processor_.process("привет мир   ");
    ASSERT_EQ(actions.size(), 1);
    EXPECT_TRUE(std::holds_alternative<TextAction>(actions[0]));
    auto text_action = std::get<TextAction>(actions[0]);
    // Trailing spaces should be removed
    EXPECT_EQ(text_action.text, "Привет мир");
}

// Test fragment spacing - fragments should be separated by single space
TEST_F(PostProcessTest, FragmentSpacingNoTrailingSpace) {
    // Process first fragment
    auto actions1 = processor_.process("привет");
    ASSERT_EQ(actions1.size(), 1);
    EXPECT_TRUE(std::holds_alternative<TextAction>(actions1[0]));
    auto text_action1 = std::get<TextAction>(actions1[0]);
    EXPECT_EQ(text_action1.text, "Привет");
    
    // Process second fragment (should have space before it)
    auto actions2 = processor_.process("мир");
    ASSERT_EQ(actions2.size(), 1);
    EXPECT_TRUE(std::holds_alternative<TextAction>(actions2[0]));
    auto text_action2 = std::get<TextAction>(actions2[0]);
    // Should have space before second fragment
    EXPECT_EQ(text_action2.text, " мир");
}

TEST_F(PostProcessTest, FragmentSpacingWithTrailingSpace) {
    // Process first fragment with trailing space
    auto actions1 = processor_.process("привет ");
    ASSERT_EQ(actions1.size(), 1);
    EXPECT_TRUE(std::holds_alternative<TextAction>(actions1[0]));
    auto text_action1 = std::get<TextAction>(actions1[0]);
    // Trailing space should be removed
    EXPECT_EQ(text_action1.text, "Привет");
    
    // Process second fragment (should have space before it, but not double)
    auto actions2 = processor_.process("мир");
    ASSERT_EQ(actions2.size(), 1);
    EXPECT_TRUE(std::holds_alternative<TextAction>(actions2[0]));
    auto text_action2 = std::get<TextAction>(actions2[0]);
    // Should have single space before second fragment (not double)
    EXPECT_EQ(text_action2.text, " мир");
}

TEST_F(PostProcessTest, FragmentSpacingMultipleTrailingSpaces) {
    // Process first fragment with multiple trailing spaces
    auto actions1 = processor_.process("привет   ");
    ASSERT_EQ(actions1.size(), 1);
    EXPECT_TRUE(std::holds_alternative<TextAction>(actions1[0]));
    auto text_action1 = std::get<TextAction>(actions1[0]);
    // Multiple trailing spaces should be removed
    EXPECT_EQ(text_action1.text, "Привет");
    
    // Process second fragment
    auto actions2 = processor_.process("мир");
    ASSERT_EQ(actions2.size(), 1);
    EXPECT_TRUE(std::holds_alternative<TextAction>(actions2[0]));
    auto text_action2 = std::get<TextAction>(actions2[0]);
    // Should have single space (not multiple)
    EXPECT_EQ(text_action2.text, " мир");
}

TEST_F(PostProcessTest, FragmentSpacingNoSpaceBeforePunctuation) {
    // Process first fragment
    auto actions1 = processor_.process("привет");
    ASSERT_EQ(actions1.size(), 1);
    EXPECT_TRUE(std::holds_alternative<TextAction>(actions1[0]));
    
    // Process second fragment starting with punctuation
    auto actions2 = processor_.process("точка мир");
    ASSERT_EQ(actions2.size(), 1);
    EXPECT_TRUE(std::holds_alternative<TextAction>(actions2[0]));
    auto text_action2 = std::get<TextAction>(actions2[0]);
    // Should not have space before punctuation
    EXPECT_EQ(text_action2.text, ". Мир");
}

TEST_F(PostProcessTest, FragmentSpacingNoSpaceBeforePunctuationWithTrailingSpace) {
    // Process first fragment with trailing space
    auto actions1 = processor_.process("привет ");
    ASSERT_EQ(actions1.size(), 1);
    
    // Process second fragment starting with punctuation
    auto actions2 = processor_.process("точка мир");
    ASSERT_EQ(actions2.size(), 1);
    EXPECT_TRUE(std::holds_alternative<TextAction>(actions2[0]));
    auto text_action2 = std::get<TextAction>(actions2[0]);
    // Should not have space before punctuation (trailing space from first fragment should be ignored)
    EXPECT_EQ(text_action2.text, ". Мир");
}
