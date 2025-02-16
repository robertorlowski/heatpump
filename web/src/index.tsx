import { Component, render } from 'preact';
import { LocationProvider, Router, Route } from 'preact-iso';

import { Header } from './components/Header.jsx';
import { NotFound } from './pages/_404.jsx';
import "./style.css"
import Settings from './pages/Settings/index.js';
import HP from './pages/HP/index.js';

export default class App extends Component {
	state = {
	//   islogged: true,
	  url: "/"
	};
  
	handleRoute = (url: string) => {
	  this.setState(() => {
		return { url };
	  });
	};
  
	render({}, { logged, url }) {
	//   if (!islogged) return <Login />;
	  return (
 		<LocationProvider>
 			<Header />
			<main>
 				<Router onRouteChange={this.handleRoute}>
 					<Route path="/" component={HP} />
 					<Route path="/settings" component={Settings}/>
 					<Route default component={NotFound} />
 				</Router>
 			</main>
 		</LocationProvider>
	  );
	}
  }

// export function App() {

// 	return (
// 		<LocationProvider>
// 			<Header />
// 			<main>
// 				<Router>
// 					<Route path="/" component={HP} />
// 					<Route path="/settings" component={Settings} />
// 					<Route default component={NotFound} />
// 				</Router>
// 			</main>
// 		</LocationProvider>
// 	);
// }

render(<App />, document.getElementById('app'));
