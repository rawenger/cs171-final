#pragma once

#include <chrono>
#include <map>
#include <ostream>
#include <string>
#include <variant>
#include <vector>

#include <cereal/archives/portable_binary.hpp>

auto dummy_transactions(std::ostream &out) -> void;

class blag
{
        private:

        using user = std::string;
        using content = std::string;
	using timestamp = std::chrono::nanoseconds;

        using comment = std::pair<user, content>;
        using post = struct {
                user author;
                content title;
                content body;
                std::vector<comment> comments;
                timestamp stamp;
        };

	// TODO: Discriminating posts by nanosecond resolution is a Very Bad Idea (TM).
	std::map<timestamp, post> posts;

	auto find_post_with_title(const content &title) -> post *;

	public:

        struct post_transaction {
                user author;
                content title;
                content body;

		template <class Archive>
		void serialize(Archive &ar) {
			ar(author, title, body);
		}
        };

        struct comment_transaction {
                user commenter;
                content title;
                content comment;

		template <class Archive>
		void serialize(Archive &ar) {
			ar(commenter, title, comment);
		}
        };

	using transaction = std::variant<post_transaction, comment_transaction>;

        auto transact(transaction trans) -> void;

        // Make a new blog post identified by the given title. 
        auto new_post(user author, content title, content body) -> void;

        // Make a new comment under the blog post with the given title.
        auto new_comment(content title, user commenter, content comment) -> void;

        // List the title and author of all blog posts in chronological order.
        auto all_posts(std::ostream &out) -> void;

        // List the title and content of all blog posts made by this user in chronological order.
        auto all_posts_by(user author, std::ostream &out) -> void;

        // List the content of the given blog post and its comments, including commenter and content.
        auto view_comments(content title, std::ostream &out) -> void;
};
