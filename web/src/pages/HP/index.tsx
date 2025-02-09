import {Component} from 'preact';
import './style.css';
import '../../api/api'
import { HpRequests } from '../../api/api';
import { TCO, THP, TPV } from '../../api/type';


function format(num?: Number) :String {
	if (num == undefined) {
		return "";
	}
	return num.toString().padStart(2, '0');
}

interface IState {
    data?: TCO;
}

export default class HP extends Component<{}, IState> {

	componentWillMount() {
		HpRequests.getCoData().then(resp => {
			this.setState({ data: resp });
		   });
	};

	render({}, {}) {
		if (!this.state.data) {	
			return 'Loading...';
		}
		return (
			<div class="settings">
				<h2>Dane centralnego ogrzewania</h2>
				<section>
					<div  class="resource">
						<h3>Ogrzewanie</h3>
						<hr/>
					</div>

					<br/>
					<div  class="resource">
						<h3>Pompa ciepła</h3>
						<hr/>
					</div>

					<br/>
					<div class="resource">
						<h3>Instalacja fotowtaiczna</h3>
						<hr/>
					</div>
				</section>
			</div>
		)
	}

}


