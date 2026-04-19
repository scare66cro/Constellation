REDUX SELECTORS
See for additional details https://redux.js.org/usage/deriving-data-selectors

Definition:
A selector is a function that extracts some value or calculated value from a part of the redux state
example1:

const selectStateStatus = state => state._status
const selectShoppingCart = state => state.shoppingCart
const selectShoppingCartTotal = state => selectShoppingCart(state).reduce((item, runTotal) => item.value + runTotal, 0)

state = {
    _status: 'idle',
    shoppingCart: [
        {
            id:'1',
            name:'apple',
            value: 2
        },
        {
            id:'2',
            name:'milk',
            value: 5
        },
    ]
}

SELECTOR BASICS 
- Use selector functions to be DRY in how you access variables in the redux state, that way when changes need to be made it only happens in a few places.

- Avoid writing selector functions inside react-component useSelector() hooks or anywhere besides the redux file for selectors:
    // BAD
        const _status = useSelector(state => state._status) //what if _status moves? every reference of state._status must be changed
    // GOOD
        import selectStateStatus from '<file-location>'
        const _status = useSelector(selectStateStatus)
    NOTE: This helps prevent the need to hunt down all the random react-components that used a state variable when a state schema change is made.
    ...Selectors can be used in anywhere the root state object can be passed through it.

- Reuse selectors in other selctor functions to prevent mapping out nested accessors in multiple places. 
    Like in Example1: 'selectShoppingCartTotal' reuses previous selector 'selectShoppingCart'

- Derive values via selectors rather than save them in state as new values, like 'selectShoppingCartTotal' in example1. (this includes filtered lists)

OPTIMIZATION (memoization)
- Selectors used with 'useSelector' or 'mapState' will be re-run after every dispatched action, regardless of what section of
... the Redux root state was actually updated useSelector and mapState rely on === reference equality checks of the return 
... values to determine if the component needs to re-render. If a selector always returns new references, it will force the
... component to re-render even if the derived data is effectively the same as last time. This is especially common with 
... array operations like map() and filter(), which return new array references.

- Because recalculated values can end up with a new pointer but never the less the same value, we need a way to write optimized
... selectors that can avoid recalculating results if the same inputs are passed in. This is where the idea of memoization comes in.

https://redux.js.org/usage/deriving-data-selectors#createselector-overview
- 'createSelector' from reselect library (automatically included with redux-tools), it uses caching behind the scenes:
    'createSelector' recieves many 'input selectors' and 1 'output selector' function (aka transform function).
    If any of the input pointers have changed since the last time the selector was called (checked using  === ), then the 
    ... output function will be recalculated using the latest inputs.
    **This means that "input selectors" should usually just extract and return values, and the "output selector" should do the transformation work.
    Example2:
        // BAD: this will not memoize correctly, and does nothing useful! This is because even if state.todos did change there is no expensive calculation to output 
        const brokenSelector = createSelector(
            state => state.todos,
            todos => todos
        )
        // BAD: similiar to the last snippet, never pass 'state => state' as an input. This will run every time because it
        // ... is referencing the root state of the app which IS changed everytime an action fires regardless of if your output will be different.
        const brokenSelector = createSelector(
            state => state,
            state => state.todos
        )
        // GOOD: only use 'createSelector' when you have an expensive calculation and there are specific inputs that affect it. 
        // ... ** verify the validity of this statement. It is Clint's intpretation.
        const goodSelector = createSelector(
            [sum1, sum2],
            averageSum,
        )

- 'createSelector' ONLY memoizes the most recent useage of a selector.
    Example1:
        const a = someSelector(state, 1) // first call, not memoized
        const b = someSelector(state, 1) // same inputs, memoized
        const c = someSelector(state, 2) // different inputs, not memoized
        const d = someSelector(state, 1) // different inputs from last time, not memoized

- 

- https://github.com/reduxjs/reselect#accessing-react-props-in-selectors
- Passing parameters -> WARNING https://redux.js.org/usage/deriving-data-selectors#createselector-behavior
    ... passed parameters are attempted on every input selector, so if they are expecting different parameters they will break when an
    ... incorrect parameter is passed in.
    EXAMPLE:
        const selectItems = state => state.items
        const selectItemId = (state, itemId) => itemId

        const selectItemById = createSelector(
        [selectItems, selectItemId],
        (items, itemId) => items[itemId]
        )

        const item = selectItemById(state, 42) // 'selectItems' does not accept a parameter so it will break when it receives 42

        /*
            Internally, Reselect does something like this:

            const firstArg = selectItems(state, 42);  
            const secondArg = selectItemId(state, 42);  
            
            const result = outputSelector(firstArg, secondArg);  
            return result;  
        */