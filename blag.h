#pragma once

#include <chrono>
#include <map>
#include <ostream>
#include <string>
#include <variant>
#include <vector>

#include <cereal/archives/portable_binary.hpp>
#include <cereal/types/variant.hpp>
#include <cereal/types/string.hpp>

class blag {
private:
        static constexpr size_t max_string_size = 128;
        using user = std::string;
        using content = std::string;
	using timestamp = std::chrono::system_clock::time_point;

        using raw_comment = std::pair<std::string, content>;

        struct comment {
                std::string user;
                std::string content;
        };

        struct post {
                std::string author {};
                std::string title {};
                std::string body {};
                std::vector<comment> comments {};
                timestamp stamp;
        };

        blag() = default;

	std::map<timestamp, post> posts;

	auto find_post_with_title(std::string_view title) -> post *;

public:
        static blag BLAG;

        // TODO: this would be much cleaner if done using polymorphism
        struct transaction {
        protected:
            static constexpr size_t format_trim_length = 8;
        public:
            enum TYPE : bool {
                POST,
                COMMENT,
            };

            std::string author;
            std::string title;
            std::string content;

            transaction(const std::string_view &author,
                        const std::string_view &title,
                        const std::string_view &content)
            : author(author), title(title), content(content)
            { }

            [[nodiscard]] virtual std::string formatter() const = 0;
            virtual std::shared_ptr<transaction> allocate() = 0;

            virtual ~transaction() = 0;

            template <class Archive>
            void serialize(Archive &ar) {
                ar(author, title, content);
            }

            virtual void add_to_blag(blag &b) const = 0;

            [[nodiscard]] virtual TYPE get_type() const = 0;

            friend std::string format_as(const transaction *tr)
            { tr->formatter(); }
        };

        struct post_transaction : public transaction {
            post_transaction(const std::string_view &author,
                                const std::string_view &title,
                                const std::string_view &content)
            : transaction(author, title, content)
            { }

            ~post_transaction() override = default;

            [[nodiscard]] TYPE get_type() const override
            { return transaction::POST; }

            void add_to_blag(blag &b) const override;
            [[nodiscard]] std::string formatter() const override;

            std::shared_ptr<transaction> allocate() override
            { return std::make_shared<post_transaction>(std::move(*this)); }
        };

        struct comment_transaction : public transaction {
            comment_transaction(const std::string_view &author,
                                const std::string_view &title,
                                const std::string_view &content)
            : transaction(author, title, content)
            { }

            ~comment_transaction() override = default;

            [[nodiscard]] TYPE get_type() const override
            { return transaction::COMMENT; }

            void add_to_blag(blag &b) const override;
            [[nodiscard]] std::string formatter() const override;

            std::shared_ptr<transaction> allocate() override
            { return std::make_shared<comment_transaction>(std::move(*this)); }
        };

        blag(const blag &other) = delete;
        blag(blag &&other) = delete;

        auto transact(const transaction &trans) -> void;

        // Make a new blog post identified by the given title. 
//        auto new_post(user author, content title, content body) -> void;

        // Make a new comment under the blog post with the given title.
//        auto new_comment(content title, user commenter, content comment) -> void;

        // List the title and author of all blog posts in chronological order.
        auto all_posts(std::ostream &out) -> void;

        // List the title and content of all blog posts made by this user in chronological order.
        auto all_posts_by(std::string_view author, std::ostream &out) -> void;

        // List the content of the given blog post and its comments, including commenter and content.
        auto view_comments(std::string_view title, std::ostream &out) -> void;
};

std::string format_as(const blag::post_transaction &pt);
std::string format_as(const blag::comment_transaction &ct);
//std::string format_as(const blag::transaction &tr);
