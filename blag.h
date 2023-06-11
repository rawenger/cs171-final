#pragma once

#include <chrono>
#include <map>
#include <ostream>
#include <string>
#include <variant>
#include <vector>

#include <cereal/archives/portable_binary.hpp>
#include <cereal/types/variant.hpp>

class blag {
private:
        static constexpr size_t max_string_size = 128;
        using user = char[max_string_size];
        using content = char[max_string_size];
	using timestamp = std::chrono::system_clock::time_point;

        using raw_comment = std::pair<user, content>;

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
        struct post_transaction {
                post_transaction(std::string &&author,
                                 std::string &&title,
                                 std::string &&body)
                {
                    std::strncpy(this->author, author.c_str(), max_string_size);
                    std::strncpy(this->title, title.c_str(), max_string_size);
                    std::strncpy(this->body, body.c_str(), max_string_size);
                }

//                post_transaction(user author, content title, content body)
//                {
//                    std::strncpy(this->author, author, max_string_size);
//                    std::strncpy(this->title, title, max_string_size);
//                    std::strncpy(this->body, body, max_string_size);
//                }

                post_transaction() = default;

                user author {0};
                content title {0};
                content body {0};

		template <class Archive>
		void serialize(Archive &ar) {
			ar(author, title, body);
		}
        };

        struct comment_transaction {
                comment_transaction(std::string &&commenter,
                                    std::string &&title,
                                    std::string &&comment)
                {
                    std::strncpy(this->commenter, commenter.c_str(), max_string_size);
                    std::strncpy(this->title, title.c_str(), max_string_size);
                    std::strncpy(this->comment, comment.c_str(), max_string_size);
                }

                comment_transaction() = default;

                user commenter {0};
                content title {0};
                content comment {0};

		template <class Archive>
		void serialize(Archive &ar) {
			ar(commenter, title, comment);
		}
        };

	using transaction = std::variant<post_transaction, comment_transaction>;

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
std::string format_as(const blag::transaction &tr);
