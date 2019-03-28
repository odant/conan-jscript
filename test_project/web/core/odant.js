// Test for jscript Conan package manager
// Dmitriy Vetutnev, Odant, 2018


console.log("I`m fake Odant framework!");


var ret = new Promise((resolve, reject) => {
    setTimeout(() => {
        resolve(42);
    }, 100);
});

module.exports = ret;
