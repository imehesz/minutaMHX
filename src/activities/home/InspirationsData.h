#pragma once

#include <cstddef>

// Built-in Inspirations: famous quotes and classic haiku. Lives in flash.
// '\n' in text forces a line break (each haiku line is kept on its own line).
// Haiku are MinutaMHX's own translations of the public-domain Japanese originals.
struct Inspiration {
  const char* text;
  const char* author;
  bool haiku;
};

constexpr Inspiration INSPIRATIONS[] = {
    // Quotes
    {"“The journey of a thousand miles begins with a single step.”", "Lao Tzu", false},
    {"“We suffer more often in imagination than in reality.”", "Seneca", false},
    {"“The impediment to action advances action. What stands in the way becomes the way.”", "Marcus Aurelius",
     false},
    {"“Waste no more time arguing about what a good man should be. Be one.”", "Marcus Aurelius", false},
    {"“The happiness of your life depends upon the quality of your thoughts.”", "Marcus Aurelius", false},
    {"“Dwell on the beauty of life. Watch the stars, and see yourself running with them.”", "Marcus Aurelius",
     false},
    {"“No man ever steps in the same river twice, for it’s not the same river and he’s not the same man.”",
     "Heraclitus", false},
    {"“Well done is better than well said.”", "Benjamin Franklin", false},
    {"“Energy and persistence conquer all things.”", "Benjamin Franklin", false},
    {"“Whether you think you can, or you think you can’t — you’re right.”", "Henry Ford", false},
    {"“Life is like riding a bicycle. To keep your balance you must keep moving.”", "Albert Einstein", false},
    {"“It always seems impossible until it’s done.”", "Nelson Mandela", false},
    {"“Do what you can, with what you have, where you are.”", "Theodore Roosevelt", false},
    {"“Believe you can and you’re halfway there.”", "Theodore Roosevelt", false},
    {"“The best way out is always through.”", "Robert Frost", false},
    {"“Go confidently in the direction of your dreams. Live the life you have imagined.”", "Henry David Thoreau",
     false},
    {"“Not all those who wander are lost.”", "J.R.R. Tolkien", false},
    {"“Act as if what you do makes a difference. It does.”", "William James", false},
    {"“Nothing in life is to be feared, it is only to be understood.”", "Marie Curie", false},
    {"“Be yourself; everyone else is already taken.”", "Oscar Wilde", false},
    {"“To live is the rarest thing in the world. Most people exist, that is all.”", "Oscar Wilde", false},
    {"“Fall seven times, stand up eight.”", "Japanese proverb", false},
    {"“Little by little, one goes far.”", "Spanish proverb", false},
    {"“A smooth sea never made a skilled sailor.”", "English proverb", false},
    {"“Even the darkest night will end and the sun will rise.”", "Victor Hugo", false},
    {"“Keep your face always toward the sunshine — and shadows will fall behind you.”", "Walt Whitman", false},
    {"“I am not afraid of storms, for I am learning how to sail my ship.”", "Louisa May Alcott", false},
    {"“The secret of getting ahead is getting started.”", "Mark Twain", false},
    {"“Kindness is the language which the deaf can hear and the blind can see.”", "Mark Twain", false},
    {"“Happiness is not something ready made. It comes from your own actions.”", "Dalai Lama", false},
    {"“If you’re going through hell, keep going.”", "Winston Churchill", false},
    {"“Every artist was first an amateur.”", "Ralph Waldo Emerson", false},
    {"“Write it on your heart that every day is the best day in the year.”", "Ralph Waldo Emerson", false},
    {"“Hope is the thing with feathers that perches in the soul.”", "Emily Dickinson", false},
    {"“There is no greater agony than bearing an untold story inside you.”", "Maya Angelou", false},
    {"“You can’t use up creativity. The more you use, the more you have.”", "Maya Angelou", false},
    {"“Let everything happen to you: beauty and terror. Just keep going. No feeling is final.”",
     "Rainer Maria Rilke", false},
    {"“And now that you don’t have to be perfect, you can be good.”", "John Steinbeck", false},
    {"“Perfection is achieved not when there is nothing more to add, but when there is nothing left to take "
     "away.”",
     "Antoine de Saint-Exupéry", false},
    {"“Start where you are. Use what you have. Do what you can.”", "Arthur Ashe", false},
    {"“A year from now you may wish you had started today.”", "Karen Lamb", false},
    {"“You miss 100% of the shots you don’t take.”", "Wayne Gretzky", false},
    {"“He who has a why to live can bear almost any how.”", "Friedrich Nietzsche", false},
    {"“We are what we repeatedly do.”", "Will Durant", false},
    {"“How we spend our days is, of course, how we spend our lives.”", "Annie Dillard", false},
    {"“What you do makes a difference, and you have to decide what kind of difference you want to make.”",
     "Jane Goodall", false},
    {"“Turn your wounds into wisdom.”", "Oprah Winfrey", false},

    // Haiku
    {"An old pond —\na frog leaps in,\nthe sound of water.", "Matsuo Bashō", true},
    {"Such stillness —\nthe cicada’s cry\nseeps into the rocks.", "Matsuo Bashō", true},
    {"Summer grasses —\nall that remains\nof warriors’ dreams.", "Matsuo Bashō", true},
    {"Sick on a journey,\nmy dreams go wandering\nover withered fields.", "Matsuo Bashō", true},
    {"Along this road\nno one is travelling —\nautumn evening.", "Matsuo Bashō", true},
    {"On a bare branch\na crow has settled —\nautumn evening.", "Matsuo Bashō", true},
    {"First winter rain —\neven the monkey seems\nto want a straw coat.", "Matsuo Bashō", true},
    {"The spring sea —\nall day long it rises and falls,\nrises and falls.", "Yosa Buson", true},
    {"Rapeseed blossoms —\nthe moon in the east,\nthe sun in the west.", "Yosa Buson", true},
    {"On the temple bell\na butterfly has settled,\nfast asleep.", "Yosa Buson", true},
    {"This world of dew\nis a world of dew —\nand yet, and yet…", "Kobayashi Issa", true},
    {"Skinny frog,\ndon’t give up!\nIssa is here.", "Kobayashi Issa", true},
    {"Little sparrow,\nout of the way, out of the way —\nthe horse is coming through.", "Kobayashi Issa", true},
    {"Don’t swat it!\nThe fly is wringing its hands,\nwringing its feet.", "Kobayashi Issa", true},
    {"Come and play with me,\nlittle sparrow\nwith no parents.", "Kobayashi Issa", true},
    {"A great firefly\ndrifting, drifting\npasses by.", "Kobayashi Issa", true},
    {"The snow has melted —\nthe whole village\nis full of children.", "Kobayashi Issa", true},
    {"Little snail,\nclimb slowly, slowly —\nall the way up Mount Fuji!", "Kobayashi Issa", true},
    {"I bite a persimmon —\na bell rings out\nat Hōryū-ji.", "Masaoka Shiki", true},
    {"The morning glory\nhas taken my well bucket —\nI’ll borrow water.", "Fukuda Chiyo-ni", true},
    {"My barn burned down —\nnow nothing stands between me\nand the moon.", "Mizuta Masahide", true},
};

constexpr size_t INSPIRATION_COUNT = sizeof(INSPIRATIONS) / sizeof(INSPIRATIONS[0]);
