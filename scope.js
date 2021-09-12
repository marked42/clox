;(function example1() {
	var a = 1

	function inner() {
	  console.log('example1: ', a)

	  var a = 2
	}
	inner()
  })()
  ;(function example2() {
	var a = 1

	function inner() {
	  console.log('example2: ', a)

	  var a
	}
	inner()
  })()
  ;(function example3() {
	var a = 1

	// 动态作用域
	function inner() {
	  with ({}) {
		console.log('example3: ', a)
	  }
	}
	inner()
  })()
  ;(function example2() {
	var a = 1

	// 静态作用域
	function inner() {
	  console.log('example2: ', typeof a)

	  let a = '2'
	}
	inner()
  })()

  // undeclared
  // typeof a === 'undefined'
  // a === 'undefined'

  var x = "global"; fun outer() { var x = "outer"; fun inner() { print x; } inner(); } outer();
